// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/SkinView.h>
#include <WasabiQt/SkinXml.h>
#include <WasabiQt/SkinRuntime.h>
#include <WasabiQt/TreePainter.h>

#include <QMetaObject>
#include <QPainter>
#include <QPaintEvent>
#include <QPointer>

namespace WasabiQt {

SkinView::SkinView(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent, false);

    // M14c: hook the runtime's repaint callback so script mutations
    // (setXmlParam from Maki) trigger a repaint. QPointer keeps the
    // closure safe even if the view dies before the callback fires,
    // and QueuedConnection routes the update to the GUI thread no
    // matter where dispatch ran.
    QPointer<SkinView> self(this);
    registerSkinRepaintCallback([self]() {
        if (auto *v = self.data()) {
            QMetaObject::invokeMethod(v, [v]() { v->update(); },
                                      Qt::QueuedConnection);
        }
    });
}

SkinView::~SkinView() {
    // M14c: clear the runtime callback so a freshly mutated widget
    // attr after this view dies doesn't reach into a dead lambda.
    registerSkinRepaintCallback({});
}

void SkinView::setActiveGammaset(const QString &name) {
    m_gammasets.setActiveGammaset(name);
    m_registry.setGammasetRegistry(&m_gammasets);   // clears tint cache
    m_fonts.invalidateGlyphCache();                 // glyphs need re-tinting too
    update();
}

void SkinView::rebuildWindowRegion() {
    clearMask();
    m_windowRegion = Layout::computeWindowRegion(
        m_tree, m_registry, m_nativeSize);
    update();
}

void SkinView::resizeLayoutTo(const QSize &size) {
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0)
        return;
    m_nativeSize = size;
    // Sync the layout root's w/h attrs so relatw/relath children
    // resolve against the new size on the next paint.
    m_tree.attrs.insert(QStringLiteral("w"),
                        QString::number(size.width()));
    m_tree.attrs.insert(QStringLiteral("h"),
                        QString::number(size.height()));
    resize(size);
    rebuildWindowRegion();
}

bool SkinView::load(const SkinXml::Document &doc,
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

    // Native size: prefer explicit w/h, fall back to minimum_w/h.
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
    resize(m_nativeSize);

    // Apply the skin's window region — pixels not covered by any
    // sysregion= layer get masked off so the player keeps its
    // chrome shape (rounded corners, drawer cutouts, etc.) instead
    // of leaking opaque bitmap pixels into the desktop.  An empty
    // region means the skin defines no sysregion mask, so we leave
    // the widget rectangular (clearMask) and rely on the
    // chrome bitmaps' own alpha for transparency.
    // Cache the window region.  Applied at paint time via
    // QPainter::setClipRegion — QWidget::setMask is X11/Windows
    // only, so we clip in paintEvent instead.  Pixels outside the
    // region stay transparent because the surface starts cleared.
    clearMask();
    m_windowRegion = Layout::computeWindowRegion(
        m_tree, m_registry, m_nativeSize);

    update();
    return true;
}

void SkinView::paintEvent(QPaintEvent *) {
    // Render skin into a raster buffer with the window-region clip
    // applied — QPainter's clip works reliably on QImage targets,
    // unlike the Wayland-backed widget surface where setClipRegion
    // is silently ignored on some compositors.  Then blit the
    // pre-clipped image onto the surface in CompositionMode_Source
    // so its alpha (zero outside the region) overwrites the
    // surface alpha unconditionally.
    QImage buf(size(), QImage::Format_ARGB32_Premultiplied);
    buf.fill(Qt::transparent);
    {
        QPainter bp(&buf);
        if (!m_windowRegion.isEmpty()) bp.setClipRegion(m_windowRegion);
        if (m_host) {
            TreePainter::paintTree(&bp, m_tree, m_registry, m_fonts,
                                    size(), m_host);
        } else {
            TreePainter::paintTree(&bp, m_tree, m_registry, m_fonts,
                                    size(), m_resolver);
        }
    }

    QPainter p(this);
    p.setClipping(false);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(0, 0, buf);

    // Stash the painted alpha for alphaHitTest().  Cheap (a few
    // hundred kB for a 354x510 layout), refreshed each paint so the
    // hit-test always reads the current frame's opacity.
    m_paintedAlpha = std::move(buf);
}

namespace {
// Recursively walk the resolved tree in paint order (children iterated
// in REVERSE for topmost-first traversal).  For each widget that
// contains the point AND has a non-zero alpha pixel at the point in
// the painted-alpha cache, return it.  Mirrors Layout::hitTestRec's
// recursion shape but with an alpha gate instead of just bbox containment.
const Layout::ResolvedWidget *
alphaHitRec(const Layout::ResolvedWidget &w,
            QPoint p, QPoint origin, QSize canvas,
            bool actionOnly,
            const QImage &alphaBuf,
            Layout::ImageSizeResolver imageSize,
            void *imageSizeUserdata) {
    using Layout::ResolvedWidget;

    if (w.attrs.value(QStringLiteral("visible")) ==
        QStringLiteral("0")) return nullptr;

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
    if (w.tag != QStringLiteral("layout")) {
        childOrigin = QPoint(origin.x() + r.x(), origin.y() + r.y());
    }
    if (r.width()  > 0) childCanvas.setWidth (r.width());
    if (r.height() > 0) childCanvas.setHeight(r.height());

    // Try children first (topmost-first via reverse iteration).
    for (auto it = w.children.crbegin(); it != w.children.crend(); ++it) {
        if (auto *hit = alphaHitRec(*it, p, childOrigin, childCanvas,
                                     actionOnly, alphaBuf,
                                     imageSize, imageSizeUserdata))
            return hit;
    }

    // Self-hit: filter by action= when requested, skip containers.
    const bool isContainer =
        w.tag == QStringLiteral("group") ||
        w.tag == QStringLiteral("container") ||
        w.tag == QStringLiteral("layout") ||
        w.tag == QStringLiteral("groupdef") ||
        w.tag.startsWith(QStringLiteral("wasabi_"));
    if (isContainer) return nullptr;
    if (actionOnly && !w.attrs.contains(QStringLiteral("action")))
        return nullptr;
    if (w.id.isEmpty() && w.tag != QStringLiteral("button") &&
        w.tag != QStringLiteral("togglebutton") &&
        w.tag != QStringLiteral("nstatesbutton") &&
        w.tag != QStringLiteral("slider"))
        return nullptr;

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
    if (width <= 0 || height <= 0) return nullptr;
    const QRect bbox(childOrigin.x(), childOrigin.y(), width, height);
    if (!bbox.contains(p)) return nullptr;

    // Alpha gate: read the painted pixel at this point.  Non-zero
    // alpha means this widget actually painted something opaque here,
    // so the click is meant for it.  Transparent → keep looking
    // (handled by the caller's tree walk; we just return nullptr).
    if (!alphaBuf.isNull() &&
        p.x() >= 0 && p.x() < alphaBuf.width() &&
        p.y() >= 0 && p.y() < alphaBuf.height()) {
        const QRgb px = alphaBuf.pixel(p.x(), p.y());
        if (qAlpha(px) <= 16) return nullptr;
    }

    return &w;
}
}  // namespace

const Layout::ResolvedWidget *
SkinView::alphaHitTest(QPoint pointInLayout, bool actionOnly,
                        Layout::ImageSizeResolver imageSize,
                        void *imageSizeUserdata) const {
    if (m_paintedAlpha.isNull()) {
        // No alpha buffer yet (first frame) — fall through to bbox-only.
        return Layout::hitTest(m_tree, pointInLayout, actionOnly,
                                imageSize, imageSizeUserdata, nullptr);
    }
    return alphaHitRec(m_tree, pointInLayout, QPoint(0, 0),
                        m_nativeSize, actionOnly, m_paintedAlpha,
                        imageSize, imageSizeUserdata);
}

namespace {
// Walk the tree top-down; collect every widget at `p` whose painted
// alpha at p is non-zero (or every widget at p if alphaBuf is null).
// Caller iterates topmost-first.  Mirrors alphaHitRec's filter rules
// (containers skipped, action-only honoured, image-size resolved).
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

    // Composite alpha gate: if the layout's painted alpha at p is
    // transparent, NOTHING is opaque there — neither this widget nor
    // any widget we'd add later.  Skip.  When alpha is opaque, we
    // can't tell which widget painted that pixel without per-widget
    // alpha, so add every bbox-containing widget — the caller picks
    // the first one with a Maki handler / built-in action.
    if (!alphaBuf.isNull() &&
        p.x() >= 0 && p.x() < alphaBuf.width() &&
        p.y() >= 0 && p.y() < alphaBuf.height()) {
        const QRgb px = alphaBuf.pixel(p.x(), p.y());
        if (qAlpha(px) <= 16) return;
    }

    out.append(&w);
}
}  // namespace

QList<const Layout::ResolvedWidget *>
SkinView::alphaHitTestList(QPoint pointInLayout, bool actionOnly,
                            Layout::ImageSizeResolver imageSize,
                            void *imageSizeUserdata) const {
    QList<const Layout::ResolvedWidget *> out;
    alphaHitListRec(m_tree, pointInLayout, QPoint(0, 0), m_nativeSize,
                     actionOnly, m_paintedAlpha,
                     imageSize, imageSizeUserdata, out);
    return out;
}

}  // namespace WasabiQt
