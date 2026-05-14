// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/HitCtx.h>
#include <WasabiQt/SkinView.h>
#include <WasabiQt/SkinXml.h>
#include <WasabiQt/SkinRuntime.h>
#include <WasabiQt/TreePainter.h>
#include <WasabiQt/Widget.h>

#include <QMetaObject>
#include <QPainter>
#include <QPaintEvent>
#include <QPointer>
#include <QVariantAnimation>

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
            QMetaObject::invokeMethod(v, [v]() {
                // Maki mutated a widget attribute — recompute the
                // window region (and auto-shrink if enabled) before
                // repainting.  Without this, a script that hides the
                // drawer via setXmlParam("y", "-263") moves the
                // sysregion contributor off-screen but the QWidget's
                // mask/size never refresh.
                v->rebuildWindowRegion();
            }, Qt::QueuedConnection);
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
    m_windowRegion = Layout::computeWindowRegion(
        m_tree, m_registry, m_nativeSize);
    // On Wayland (Qt 6.x), QWidget::setMask is forwarded to
    // wl_surface.set_input_region, so transparent areas of the chrome
    // become click-through to the desktop / windows behind us.  On
    // X11/Windows it's the conventional window-shape mask.  We keep
    // the paint-side clip in paintEvent for compositors that ignore
    // the input region — that ensures the visual is correct either
    // way, and setMask just adds the input-region behaviour where the
    // compositor honours it.  Empty region = rectangular widget.
    if (m_windowRegion.isEmpty())
        clearMask();
    else
        setMask(m_windowRegion);
    update();
}

// Compute the bottom-most row with a non-zero alpha pixel.  Used by
// auto-shrink to crop the OS window to the actual painted extent.
namespace {
int paintedBottomEdge(const QImage &alpha) {
    if (alpha.isNull()) return -1;
    for (int y = alpha.height() - 1; y >= 0; --y) {
        for (int x = 0; x < alpha.width(); ++x) {
            if (qAlpha(alpha.pixel(x, y)) > 16) return y + 1;
        }
    }
    return -1;
}
}  // namespace

void SkinView::setPaintedAlpha(QImage img) {
    m_paintedAlpha = std::move(img);
    if (!m_autoShrink || m_paintedAlpha.isNull()) return;

    // Find the actual painted bottom edge and shrink the QWidget if
    // it's significantly shorter than the current widget height.
    // Never grow — Maki-driven setTargetH owns the layout-extension
    // case via resizeLayoutTo.  An 8-px hysteresis avoids sub-pixel
    // jitter when widgets paint at-or-near the layout edge.
    const int bottom = paintedBottomEdge(m_paintedAlpha);
    if (bottom > 0 && bottom + 8 <= height()) {
        if (::getenv("WASABIQT_TRACE_MAKI"))
            ::fprintf(stderr, "[autoshrink] %dx%d -> %dx%d (painted bottom)\n",
                      width(), height(), width(), bottom);
        resize(width(), bottom);
    }
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

void SkinView::animatedResizeLayoutTo(const QSize &target, int durationMs) {
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
    // the widget rectangular and rely on chrome bitmap alpha.
    rebuildWindowRegion();
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
// Wrap the embedder's ImageSizeResolver (which takes a userdata
// pointer) into the simpler `ImageSizeFn` callable that Widget::
// hitTest's HitCtx expects.  Returns an empty function when the
// resolver itself is null.
ImageSizeFn wrapImageSize(Layout::ImageSizeResolver imageSize, void *ud) {
    if (!imageSize) return {};
    return [imageSize, ud](const QString &id) { return imageSize(id, ud); };
}
}  // namespace

const Layout::ResolvedWidget *
SkinView::alphaHitTest(QPoint pointInLayout, bool actionOnly,
                        Layout::ImageSizeResolver imageSize,
                        void *imageSizeUserdata) const {
    HitCtx ctx;
    ctx.actionOnly = actionOnly;
    ctx.requireIdOrInteractive = true;
    ctx.imageSize = wrapImageSize(imageSize, imageSizeUserdata);
    if (!m_paintedAlpha.isNull()) ctx.alphaBuf = &m_paintedAlpha;
    QRect bbox;
    return const_cast<Widget &>(m_tree).hitTest(
        pointInLayout, QPoint(0, 0), m_nativeSize, ctx, &bbox);
}

QList<const Layout::ResolvedWidget *>
SkinView::alphaHitTestList(QPoint pointInLayout, bool actionOnly,
                            Layout::ImageSizeResolver imageSize,
                            void *imageSizeUserdata) const {
    HitCtx ctx;
    ctx.actionOnly = actionOnly;
    ctx.requireIdOrInteractive = true;
    ctx.imageSize = wrapImageSize(imageSize, imageSizeUserdata);
    if (!m_paintedAlpha.isNull()) ctx.alphaBuf = &m_paintedAlpha;
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

}  // namespace WasabiQt
