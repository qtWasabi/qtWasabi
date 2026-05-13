// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/SkinQuickItem.h>

#include <WasabiQt/SkinXml.h>
#include <WasabiQt/SkinRuntime.h>
#include <WasabiQt/TreePainter.h>
#include <WasabiQt/Host.h>

#include <QHash>
#include <QImage>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QSet>
#include <QVariantAnimation>
#include <QWindow>

namespace WasabiQt {

SkinQuickItem::SkinQuickItem(QQuickItem *parent) : QQuickItem(parent) {
    setFlag(QQuickItem::ItemHasContents, true);
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);

    // Wire the Maki repaint callback so script setXmlParam mutations
    // trigger a repaint.  QPointer + QueuedConnection match the old
    // SkinView pattern so callbacks from the VM thread land safely
    // on the GUI thread.
    QPointer<SkinQuickItem> self(this);
    registerSkinRepaintCallback([self]() {
        if (auto *v = self.data()) {
            QMetaObject::invokeMethod(v, [v]() { v->update(); },
                                      Qt::QueuedConnection);
        }
    });
}

SkinQuickItem::~SkinQuickItem() {
    registerSkinRepaintCallback({});
}

void SkinQuickItem::setActiveGammaset(const QString &name) {
    m_gammasets.setActiveGammaset(name);
    m_registry.setGammasetRegistry(&m_gammasets);
    m_fonts.invalidateGlyphCache();
    m_alphaCache.clear();
    update();
}

void SkinQuickItem::animatedResizeLayoutTo(const QSize &target, int durationMs) {
    if (!target.isValid() || target.width() <= 0 || target.height() <= 0)
        return;
    const QSize from = m_nativeSize;
    if (from == target || durationMs <= 0) {
        resizeLayoutTo(target);
        fireTargetReached();
        return;
    }
    if (!m_resizeAnim) {
        m_resizeAnim = new QVariantAnimation(this);
        m_resizeAnim->setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(m_resizeAnim, &QVariantAnimation::valueChanged,
            this, [this](const QVariant &v) {
                resizeLayoutTo(v.toSize());
            });
        QObject::connect(m_resizeAnim, &QVariantAnimation::finished,
            this, []() { fireTargetReached(); });
    } else {
        m_resizeAnim->stop();
    }
    m_resizeAnim->setStartValue(from);
    m_resizeAnim->setEndValue(target);
    m_resizeAnim->setDuration(durationMs);
    beginAnimatedResize();
    m_resizeAnim->start();
}

void SkinQuickItem::resizeLayoutTo(const QSize &size) {
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0) return;
    m_nativeSize = size;
    m_tree.attrs.insert(QStringLiteral("w"),
                        QString::number(size.width()));
    m_tree.attrs.insert(QStringLiteral("h"),
                        QString::number(size.height()));
    setSize(QSizeF(size));
    // Also resize the hosting QQuickWindow so the OS surface grows to
    // the new layout.  Auto-shrink in updatePaintNode will trim back
    // down to the painted extent on the next frame if it's smaller.
    if (auto *w = window()) {
        w->resize(size);
    }
    m_alphaCache.clear();
    rebuildWindowRegion();
    emit layoutNativeSizeChanged();
    update();
}

bool SkinQuickItem::load(const SkinXml::Document &doc,
                          const QString &containerId,
                          const QString &layoutId,
                          QString *errMsg) {
    if (!Layout::expandLayout(doc, containerId, layoutId, m_tree, errMsg))
        return false;
    m_registry.loadFromDocument(doc);
    m_fonts.loadFromDocument(doc);
    m_gammasets.loadFromDocument(doc);
    m_colors.loadFromDocument(doc);
    m_registry.setGammasetRegistry(&m_gammasets);

    auto attrInt = [&](const QString &k, int def = 0) {
        auto it = m_tree.attrs.constFind(k);
        if (it == m_tree.attrs.constEnd()) return def;
        bool ok = false;
        int v = it.value().toInt(&ok);
        return ok ? v : def;
    };
    int w = attrInt(QStringLiteral("w"));
    int h = attrInt(QStringLiteral("h"));
    if (w <= 0) w = attrInt(QStringLiteral("minimum_w"), 354);
    if (h <= 0) h = attrInt(QStringLiteral("minimum_h"), 280);
    m_nativeSize = QSize(w, h);
    setSize(QSizeF(m_nativeSize));
    m_alphaCache.clear();
    rebuildWindowRegion();

    emit layoutNativeSizeChanged();
    update();
    return true;
}

// ── Scene Graph paint ───────────────────────────────────────────────
//
// Bridge approach for phase 0/1: paint the resolved widget tree to an
// offscreen QImage using the existing TreePainter, then upload the
// result as a single QSGTexture and present it via QSGSimpleTextureNode.
// Functionally identical to SkinView::paintEvent, just hosted in a
// QQuickItem container so:
//   - we benefit from the QQuickWindow's transparent + frameless flags
//   - alpha-aware hit-test via contains() override (phase 4)
//   - Wayland setMask via QWindow::setMask (phase 5)
//   - QPropertyAnimation can drive widget attrs (phase 4)
// Once the bridge works end-to-end, later phases incrementally replace
// the QImage-painter with per-widget QSGNodes (layer → QSGSimpleTextureNode,
// text → QSGGeometryNode per glyph, grid → 3-slice, vis → custom
// geometry, etc.).
void SkinQuickItem::paintInto(QPainter *p, const QSize &canvas) {
    if (m_host) {
        TreePainter::paintTree(p, m_tree, m_registry, m_fonts,
                                canvas, m_host);
    } else {
        TreePainter::paintTree(p, m_tree, m_registry, m_fonts,
                                canvas, m_resolver);
    }
}

void SkinQuickItem::rebuildWindowRegion() {
    m_windowRegion = Layout::computeWindowRegion(
        m_tree, m_registry, m_nativeSize);
    if (auto *w = window()) {
        // QQuickWindow::setMask routes to wl_surface.set_input_region
        // on Wayland (Qt 6), to the conventional shape mask on X11/
        // Windows.  Empty region = rectangular window.
        if (m_windowRegion.isEmpty()) w->setMask(QRegion());
        else                          w->setMask(m_windowRegion);
    }
    update();
}

// Bottom-most row with a non-zero alpha pixel — auto-shrink target.
static int paintedBottomEdge(const QImage &alpha) {
    if (alpha.isNull()) return -1;
    for (int y = alpha.height() - 1; y >= 0; --y) {
        for (int x = 0; x < alpha.width(); ++x) {
            if (qAlpha(alpha.pixel(x, y)) > 16) return y + 1;
        }
    }
    return -1;
}

QSGNode *SkinQuickItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
    if (m_nativeSize.isEmpty()) return nullptr;
    auto *win = window();
    if (!win) return old;

    const QSize sz = m_nativeSize;
    QImage buf(sz, QImage::Format_ARGB32_Premultiplied);
    buf.fill(Qt::transparent);
    {
        QPainter bp(&buf);
        paintInto(&bp, sz);
    }

    // Auto-shrink: trim the QQuickWindow's height to the painted
    // chrome's bottom edge so transparent areas below the chrome
    // don't bleed through to the desktop.  Never grow — Maki-driven
    // setTargetH owns that case via resizeLayoutTo.  8-px hysteresis
    // avoids sub-pixel jitter for widgets that paint at the edge.
    if (m_autoShrink && !buf.isNull()) {
        const int bottom = paintedBottomEdge(buf);
        if (bottom > 0 && win && bottom + 8 <= win->height()) {
            if (::getenv("WASABIQT_TRACE_MAKI"))
                ::fprintf(stderr,
                    "[autoshrink] %dx%d -> %dx%d (painted bottom)\n",
                    win->width(), win->height(),
                    win->width(), bottom);
            win->resize(win->width(), bottom);
        }
    }

    // Build (or recycle) the texture node.
    auto *node = static_cast<QSGSimpleTextureNode *>(old);
    if (!node) {
        node = new QSGSimpleTextureNode();
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Nearest);   // pixel-perfect bitmap chrome
    }
    QSGTexture *tex = win->createTextureFromImage(
        buf, QQuickWindow::TextureHasAlphaChannel);
    node->setTexture(tex);
    node->setRect(QRectF(0, 0, sz.width(), sz.height()));

    // Stash the painted buffer's alpha channel for hit-testing.  We
    // store the full buffer (cheap — a few hundred kB) keyed on the
    // root widget pointer so contains() can sample it directly.
    m_alphaCache.clear();
    m_alphaCache.insert(&m_tree, std::move(buf));

    return node;
}

// ── Hit-test ────────────────────────────────────────────────────────

bool SkinQuickItem::contains(const QPointF &point) const {
    // Bounds check first.
    if (!QRectF(QPointF(0, 0), QSizeF(m_nativeSize)).contains(point))
        return false;
    // If we haven't painted yet, fall through to the inclusive default
    // so the first click reaches us.  Later phases populate per-widget
    // alpha; for phase 0 we treat the painted-buffer alpha as the
    // single source of truth.
    auto it = m_alphaCache.constFind(&m_tree);
    if (it == m_alphaCache.constEnd() || it->isNull()) return true;
    const QImage &buf = it.value();
    const int x = qBound(0, int(point.x()), buf.width() - 1);
    const int y = qBound(0, int(point.y()), buf.height() - 1);
    const QRgb px = buf.pixel(x, y);
    return qAlpha(px) > 16;
}

// Shared recursive walker: collect every visible non-container widget
// at `p` whose painted alpha (composite buffer) is non-zero, topmost
// first.  Same filter rules as SkinView::alphaHitListRec.
namespace {
void alphaHitListRec(const Layout::ResolvedWidget &w,
                      QPoint p, QPoint origin, QSize canvas,
                      bool actionOnly,
                      const QImage &alphaBuf,
                      Layout::ImageSizeResolver imageSize,
                      void *imageSizeUserdata,
                      QList<const Layout::ResolvedWidget *> &out) {
    if (w.attrs.value(QStringLiteral("visible")) ==
        QStringLiteral("0")) return;

    auto attrBool = [](const QHash<QString, QString> &a, const QString &k) {
        const QString v = a.value(k);
        return v == QStringLiteral("1") ||
               v.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
    };
    auto resolveRect = [&](const QHash<QString, QString> &a, QSize parent) {
        int x = a.value(QStringLiteral("x")).toInt();
        int y = a.value(QStringLiteral("y")).toInt();
        int rw = a.value(QStringLiteral("w")).toInt();
        int rh = a.value(QStringLiteral("h")).toInt();
        if (attrBool(a, QStringLiteral("relatx"))) x = parent.width()  + x;
        if (attrBool(a, QStringLiteral("relaty"))) y = parent.height() + y;
        if (attrBool(a, QStringLiteral("relatw"))) rw = parent.width()  + rw;
        if (attrBool(a, QStringLiteral("relath"))) rh = parent.height() + rh;
        return QRect(x, y, rw, rh);
    };

    const QRect r = resolveRect(w.attrs, canvas);
    QPoint childOrigin = origin;
    QSize childCanvas = canvas;
    if (w.tag != QStringLiteral("layout"))
        childOrigin = QPoint(origin.x() + r.x(), origin.y() + r.y());
    if (r.width()  > 0) childCanvas.setWidth (r.width());
    if (r.height() > 0) childCanvas.setHeight(r.height());

    for (auto it = w.children.crbegin(); it != w.children.crend(); ++it)
        alphaHitListRec(*it, p, childOrigin, childCanvas, actionOnly,
                         alphaBuf, imageSize, imageSizeUserdata, out);

    const bool isContainer =
        w.tag == QStringLiteral("group") ||
        w.tag == QStringLiteral("container") ||
        w.tag == QStringLiteral("layout") ||
        w.tag == QStringLiteral("groupdef") ||
        w.tag.startsWith(QStringLiteral("wasabi_"));
    if (isContainer) return;
    if (actionOnly && !w.attrs.contains(QStringLiteral("action"))) return;
    if (w.id.isEmpty() && w.tag != QStringLiteral("button") &&
        w.tag != QStringLiteral("togglebutton") &&
        w.tag != QStringLiteral("nstatesbutton") &&
        w.tag != QStringLiteral("slider"))
        return;

    int width  = r.width();
    int height = r.height();
    if ((width <= 0 || height <= 0) && imageSize) {
        const QString img = w.attrs.value(QStringLiteral("image"));
        if (!img.isEmpty()) {
            const QSize is = imageSize(img, imageSizeUserdata);
            if (width  <= 0) width  = is.width();
            if (height <= 0) height = is.height();
        }
    }
    if (width <= 0 || height <= 0) return;
    const QRect bbox(childOrigin.x(), childOrigin.y(), width, height);
    if (!bbox.contains(p)) return;

    if (!alphaBuf.isNull() &&
        p.x() >= 0 && p.x() < alphaBuf.width() &&
        p.y() >= 0 && p.y() < alphaBuf.height()) {
        const QRgb px = alphaBuf.pixel(p.x(), p.y());
        if (qAlpha(px) <= 16) return;
    }
    out.append(&w);
}
}  // namespace

const Layout::ResolvedWidget *
SkinQuickItem::topmostWidgetAt(QPoint pointInLayout, bool actionOnly) const {
    auto it = m_alphaCache.constFind(&m_tree);
    const QImage &buf = (it == m_alphaCache.constEnd()) ? QImage() : *it;
    QList<const Layout::ResolvedWidget *> hits;
    alphaHitListRec(m_tree, pointInLayout, QPoint(0,0), m_nativeSize,
                     actionOnly, buf, nullptr, nullptr, hits);
    return hits.isEmpty() ? nullptr : hits.first();
}

QList<const Layout::ResolvedWidget *>
SkinQuickItem::alphaHitTestList(QPoint pointInLayout, bool actionOnly,
                                 Layout::ImageSizeResolver imageSize,
                                 void *imageSizeUserdata) const {
    auto it = m_alphaCache.constFind(&m_tree);
    const QImage &buf = (it == m_alphaCache.constEnd()) ? QImage() : *it;
    QList<const Layout::ResolvedWidget *> out;
    alphaHitListRec(m_tree, pointInLayout, QPoint(0,0), m_nativeSize,
                     actionOnly, buf, imageSize, imageSizeUserdata, out);
    return out;
}

QString SkinQuickItem::dispatchClickAt(QPointF localPoint) {
    const QPoint p = localPoint.toPoint();
    const auto hits = alphaHitTestList(p, /*actionOnly=*/false);
    for (const auto *w : hits) {
        if (!w || w->id.isEmpty()) continue;
        const int fired = fireWidgetEvent(w->id, L"onLeftClick");
        if (fired > 0) return w->id;
    }
    return QString();
}

// ── Mouse handling ──────────────────────────────────────────────────

void SkinQuickItem::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) { QQuickItem::mousePressEvent(e); return; }
    const QPointF lp = e->position();
    const QString consumedId = dispatchClickAt(lp);
    if (!consumedId.isEmpty()) {
        update();
        e->accept();
        return;
    }
    // Empty-area click → start window drag.  Wayland prefers
    // QWindow::startSystemMove; on other platforms fall back to manual
    // tracking via mouseMove.
    if (auto *w = window()) {
        if (w->startSystemMove()) {
            e->accept();
            return;
        }
        m_dragging = true;
        m_dragOriginGlobal = e->globalPosition().toPoint();
        m_dragWindowStart  = w->position();
        e->accept();
        return;
    }
    QQuickItem::mousePressEvent(e);
}

void SkinQuickItem::mouseMoveEvent(QMouseEvent *e) {
    if (m_dragging) {
        if (auto *w = window()) {
            const QPoint d = e->globalPosition().toPoint() - m_dragOriginGlobal;
            w->setPosition(m_dragWindowStart + d);
        }
        e->accept();
        return;
    }
    QQuickItem::mouseMoveEvent(e);
}

void SkinQuickItem::mouseReleaseEvent(QMouseEvent *e) {
    m_dragging = false;
    QQuickItem::mouseReleaseEvent(e);
}

}  // namespace WasabiQt
