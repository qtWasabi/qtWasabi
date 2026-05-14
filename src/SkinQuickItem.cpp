// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/SkinQuickItem.h>

#include <WasabiQt/HitCtx.h>
#include <WasabiQt/SkinXml.h>
#include <WasabiQt/SkinRuntime.h>
#include <WasabiQt/TreePainter.h>
#include <WasabiQt/Host.h>
#include <WasabiQt/Widget.h>

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
#include <QTimer>
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
            // Recompute the window region synchronously so the next
            // paint's clip reflects the just-mutated attr.  Maki
            // script dispatch runs on the GUI thread, so a direct
            // call is safe; the previous queued-update path left
            // the region stale (with drawer.y from XML defaults
            // instead of after-script values), producing the
            // "white edge rectangles on the drawer" visual bug.
            //
            // Skip the region rebuild while a tween is in flight
            // (drawer slide, layout grow): it's a tree walk + alpha
            // → QRegion pass + setMask roundtrip per frame, which
            // dominated the animation's per-frame cost and caused
            // the visible chop.  rebuildWindowRegion runs once at
            // the animation's finish handler to settle the input
            // region back to the final shape.
            if (widgetAnimationsActive() == 0 && !v->m_layoutAnimActive)
                v->rebuildWindowRegion();
            v->update();
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
            this, [this]() {
                // End of tween: clear the suppression flag, settle the
                // QQuickWindow at exact target dimensions, and rebuild
                // the input region (suppressed during the tween).
                m_layoutAnimActive = false;
                if (auto *w = window()) w->resize(m_nativeSize);
                rebuildWindowRegion();
                fireTargetReached();
            });
    } else {
        m_resizeAnim->stop();
        m_layoutAnimActive = false;
    }
    // Pre-grow the QQuickWindow once to the larger of from/target so
    // the chrome's painted extent has room throughout the animation.
    // Resizing a Wayland surface every animation tick is expensive
    // (each frame triggers a fresh xdg_surface ack + buffer attach
    // handshake) and visibly lags behind the animation, leaving the
    // chrome clipped short of the target.  resizeLayoutTo will see
    // m_layoutAnimActive=true and skip the window resize for the
    // duration of the tween.
    if (auto *w = window()) {
        const int wantH = qMax(from.height(), target.height());
        const int wantW = qMax(from.width(),  target.width());
        if (w->width() != wantW || w->height() != wantH)
            w->resize(wantW, wantH);
    }
    m_layoutAnimActive = true;
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
    // Also resize the hosting QQuickWindow so the OS surface matches the
    // new layout.  Suppressed during an active layout tween — see
    // animatedResizeLayoutTo: it pre-grows the window once at start and
    // settles it back at finish, so per-frame resizes only churn the
    // Wayland surface without changing what the user perceives.
    if (!m_layoutAnimActive) {
        if (auto *w = window()) {
            w->resize(size);
        }
    }
    m_alphaCache.clear();
    // rebuildWindowRegion is a tree walk + alpha→region pass + QQuickWindow
    // setMask roundtrip — significant per-frame cost.  Skip it during a
    // layout tween (the input region is in flux anyway while the drawer
    // slides) and let the finish handler rebuild it once at settle.
    if (!m_layoutAnimActive) rebuildWindowRegion();
    emit layoutNativeSizeChanged();
    update();
}

bool SkinQuickItem::load(const SkinXml::Document &doc,
                          const QString &containerId,
                          const QString &layoutId,
                          QString *errMsg) {
    if (!Layout::expandLayout(doc, containerId, layoutId, m_tree, errMsg))
        return false;
    m_doc = &doc;
    m_registry.loadFromDocument(doc);
    m_fonts.loadFromDocument(doc);
    m_gammasets.loadFromDocument(doc);
    m_colors.loadFromDocument(doc);
    m_registry.setGammasetRegistry(&m_gammasets);

    // Static menualign.maki equivalent: walk doc.scripts for any
    // menualign references, then lay out the named widgets in the
    // owner group side-by-side.  Mirrors menualign.m's onScriptLoaded
    // loop: `tmp.setXMLparam("x", offset); offset += tmp.getAutoWidth();`.
    // <script> elements are filtered out during expansion, so we
    // can't search the resolved tree for them — but doc.scripts
    // preserves them with ownerGroupId pointing to the enclosing
    // <groupdef>.  This runs at load time because we need the
    // BitmapRegistry for text-bitmap widths.
    {
        auto bitmapWidth = [&](const QString &imgId) {
            QImage im = m_registry.imageFor(imgId);
            return im.isNull() ? 0 : im.width();
        };
        std::function<Widget *(Widget &, const QString &)> findById =
            [&](Widget &n, const QString &id) -> Widget * {
            if (n.id == id) return &n;
            for (auto &c : n.children)
                if (c)
                    if (auto *r = findById(*c, id)) return r;
            return nullptr;
        };
        for (const auto &ref : doc.scripts) {
            if (!ref.file.contains(QStringLiteral("menualign"),
                                    Qt::CaseInsensitive))
                continue;
            // Find the owner group's widget in the live tree.
            Widget *group = findById(m_tree, ref.ownerGroupId);
            if (!group) continue;
            int offset = 0;
            for (const QString &id : ref.param.split(QChar(','),
                                                     Qt::SkipEmptyParts)) {
                const QString name = id.trimmed();
                Widget *target = nullptr;
                for (const auto &c : group->children) {
                    if (c && c->id == name) { target = c.get(); break; }
                }
                if (!target) continue;
                target->setXmlParam(QStringLiteral("x"),
                                      QString::number(offset));
                const QString aws = target->attrs.value(
                    QStringLiteral("autowidthsource"));
                int w = 0;
                if (!aws.isEmpty()) {
                    if (Widget *src = findById(*target, aws)) {
                        const QString img = src->attrs.value(
                            QStringLiteral("image"));
                        if (!img.isEmpty()) w = bitmapWidth(img);
                    }
                }
                if (w > 0) {
                    target->setXmlParam(QStringLiteral("w"),
                                          QString::number(w));
                    offset += w;
                }
            }
        }
    }
    // Pair every sysregion="-N" cutout layer with its sibling chrome
    // layer.  These are TALL narrowing-strip cutouts that get baked
    // INTO the chrome bitmap's alpha (BitmapRegistry::chromeImageFor
    // uses DestinationOut).  Without this the drawer-narrowing strips
    // along the player.main left/right edges paint as solid white
    // stripes instead of being cut away.  Only the per-bitmap bake
    // remains — the final-buffer pass (Layout::paintRegionCutouts)
    // that handled small corner masks stays disabled because it
    // damaged chrome at any other layer overlapping those corners.
    m_registry.setChromeCutouts(
        Layout::collectChromeCutouts(m_tree, m_registry));

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
    // Pushing setMask to a QQuickWindow on Wayfire/wlroots before
    // its first frame has been committed prevents the xdg-toplevel
    // from ever registering with the compositor.  Even after the
    // window is mapped, setMask can race with the surface state and
    // cause the window to disappear from foreign-toplevel
    // enumeration.  Skin the mask off for QQuickWindow path entirely
    // — the input region was a Phase 5 nice-to-have (click-through
    // on transparent chrome), not a requirement.  The visual is
    // unaffected because the QSGTexture upload already carries the
    // chrome's alpha.  WASABIQT_INPUT_REGION=1 re-enables it.
    // setMask path: defer the FIRST mask via a 100ms one-shot timer
    // (the first commit needs to land before Wayfire accepts a
    // setMask without dropping the surface), then update on every
    // subsequent region rebuild.  isExposed() never goes true for a
    // QQuickWindow on Wayfire/Asahi so we can't gate on that.
    if (!::getenv("WASABIQT_NO_MASK")) {
        if (auto *w = window()) {
            if (!m_maskInitialised) {
                m_maskInitialised = true;
                QPointer<SkinQuickItem> self(this);
                QPointer<QQuickWindow> wp(w);
                QTimer::singleShot(150, this, [self, wp]() {
                    if (auto *s = self.data(); s && wp) {
                        if (s->m_windowRegion.isEmpty()) wp->setMask(QRegion());
                        else                            wp->setMask(s->m_windowRegion);
                        if (::getenv("WASABIQT_TRACE_MASK"))
                            fprintf(stderr,
                              "[mask] FIRST setMask rectCount=%d alpha=%d\n",
                              int(s->m_windowRegion.rectCount()),
                              wp->format().alphaBufferSize());
                    }
                });
            } else {
                if (m_windowRegion.isEmpty()) w->setMask(QRegion());
                else                          w->setMask(m_windowRegion);
            }
        }
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
    // Re-instantiate any GroupXFade widget whose `groupid` attr has
    // changed since the last paint — that's how the configtarget /
    // options-page scripts switch between option pages, by mutating
    // `skin.config.target.groupid` from "optionsgroup.drawers" to
    // "optionsgroup.menus" (or whichever the user clicked).
    if (m_doc) Layout::resolveGroupXFadePages(m_tree, *m_doc);
    QImage buf(sz, QImage::Format_ARGB32_Premultiplied);
    buf.fill(Qt::transparent);
    {
        QPainter bp(&buf);
        paintInto(&bp, sz);
        // The old pixel-subtraction corner-rounding pipeline (chrome
        // bitmap pre-bake in BitmapRegistry::chromeImageFor + final-
        // buffer DestinationOut via Layout::paintRegionCutouts) is
        // now disabled by default.  The window region (setMask on
        // the QQuickWindow) handles outer corner rounding, and Maki
        // scripts drive the per-widget visibility / clipping —
        // running the legacy subtraction on top damaged chrome
        // bitmaps that happened to overlap a sysregion="-N" cutout
        // (most visibly the player.main bottom-right area near the
        // CONFIG / winamp-flash buttons after a drawer-close).
        // Set WASABIQT_LEGACY_CUTOUTS=1 to opt back in.
        if (::getenv("WASABIQT_LEGACY_CUTOUTS"))
            Layout::paintRegionCutouts(bp, m_tree, m_registry, sz);
    }

    // Auto-fit: match the QQuickWindow's height to the painted
    // chrome's bottom edge so transparent areas below the chrome
    // don't bleed through to the desktop AND so a re-grown chrome
    // (drawer reopen after a closed state's shrink) doesn't get
    // clipped by a too-small window.  8-px hysteresis avoids
    // sub-pixel jitter for widgets that paint at the edge.  Clamps
    // to m_nativeSize so we never exceed the layout's authored
    // height.
    //
    // CRITICAL: only run after Wayfire has actually mapped the
    // surface (`isExposed() == true`).  Resizing the QQuickWindow
    // from inside the first updatePaintNode (before the surface has
    // committed its first buffer) aborts the wayland commit and the
    // window stays invisible on screen.  isExposed() goes true the
    // moment the compositor signals the surface as visible.
    if (m_autoShrink && !buf.isNull() && win && win->isExposed()) {
        const int curH = win->height();
        const int curW = win->width();
        if (widgetAnimationsActive() > 0 || m_layoutAnimActive) {
            // Pre-grow the window to the full layout height for the
            // duration of any widget tween (drawer slide).  Resizing
            // a Wayland surface per frame is expensive — the queued
            // reconfigures lag behind the animation, leaving the
            // chrome clipped short of the final target.  Auto-shrink
            // resumes once the animation finishes.
            const int wantH = m_nativeSize.height();
            if (wantH > curH) {
                QMetaObject::invokeMethod(win, [win, curW, wantH]() {
                    win->resize(curW, wantH);
                }, Qt::QueuedConnection);
            }
        } else {
            const int bottom = paintedBottomEdge(buf);
            const int target = qMin(bottom, m_nativeSize.height());
            if (target > 0 && qAbs(curH - target) > 8) {
                if (::getenv("WASABIQT_TRACE_MAKI"))
                    ::fprintf(stderr,
                        "[autofit] %dx%d -> %dx%d (painted bottom %d)\n",
                        curW, curH, curW, target, bottom);
                QMetaObject::invokeMethod(win, [win, curW, target]() {
                    win->resize(curW, target);
                }, Qt::QueuedConnection);
            }
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

namespace {
ImageSizeFn wrapImageSize(Layout::ImageSizeResolver imageSize, void *ud) {
    if (!imageSize) return {};
    return [imageSize, ud](const QString &id) { return imageSize(id, ud); };
}
}  // namespace

const Layout::ResolvedWidget *
SkinQuickItem::topmostWidgetAt(QPoint pointInLayout, bool actionOnly) const {
    HitCtx ctx;
    ctx.actionOnly = actionOnly;
    ctx.requireIdOrInteractive = true;
    auto it = m_alphaCache.constFind(&m_tree);
    if (it != m_alphaCache.constEnd() && !it->isNull())
        ctx.alphaBuf = &(*it);
    QRect bbox;
    return const_cast<Widget &>(m_tree).hitTest(
        pointInLayout, QPoint(0, 0), m_nativeSize, ctx, &bbox);
}

QList<const Layout::ResolvedWidget *>
SkinQuickItem::alphaHitTestList(QPoint pointInLayout, bool actionOnly,
                                 Layout::ImageSizeResolver imageSize,
                                 void *imageSizeUserdata) const {
    HitCtx ctx;
    ctx.actionOnly = actionOnly;
    ctx.requireIdOrInteractive = true;
    ctx.imageSize = wrapImageSize(imageSize, imageSizeUserdata);
    auto it = m_alphaCache.constFind(&m_tree);
    if (it != m_alphaCache.constEnd() && !it->isNull())
        ctx.alphaBuf = &(*it);
    QList<Widget *> hits;
    ctx.collect = &hits;
    QRect bbox;
    const_cast<Widget &>(m_tree).hitTest(
        pointInLayout, QPoint(0, 0), m_nativeSize, ctx, &bbox);
    QList<const Layout::ResolvedWidget *> out;
    out.reserve(hits.size());
    for (auto *h : hits) out.append(h);
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
