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
}

}  // namespace WasabiQt
