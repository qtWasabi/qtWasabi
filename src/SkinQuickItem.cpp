// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/SkinQuickItem.h>

#include <WasabiQt/SkinXml.h>
#include <WasabiQt/SkinRuntime.h>
#include <WasabiQt/TreePainter.h>
#include <WasabiQt/Host.h>

#include <QImage>
#include <QMetaObject>
#include <QPainter>
#include <QPointer>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

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

void SkinQuickItem::resizeLayoutTo(const QSize &size) {
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0) return;
    m_nativeSize = size;
    m_tree.attrs.insert(QStringLiteral("w"),
                        QString::number(size.width()));
    m_tree.attrs.insert(QStringLiteral("h"),
                        QString::number(size.height()));
    setSize(QSizeF(size));
    m_alphaCache.clear();
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
QSGNode *SkinQuickItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
    if (m_nativeSize.isEmpty()) return nullptr;
    auto *win = window();
    if (!win) return old;

    const QSize sz = m_nativeSize;
    QImage buf(sz, QImage::Format_ARGB32_Premultiplied);
    buf.fill(Qt::transparent);
    {
        QPainter bp(&buf);
        if (m_host) {
            TreePainter::paintTree(&bp, m_tree, m_registry, m_fonts,
                                    sz, m_host);
        } else {
            TreePainter::paintTree(&bp, m_tree, m_registry, m_fonts,
                                    sz, m_resolver);
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

const Layout::ResolvedWidget *
SkinQuickItem::topmostWidgetAt(QPoint pointInLayout, bool actionOnly) const {
    // Delegate to Layout::hitTest for now; phase 4 adds an alpha-aware
    // wrapper that walks deeper when the topmost-bbox widget has a
    // transparent pixel at the click.
    QRect outBbox;
    Layout::ImageSizeResolver imgRes = nullptr;   // skin embedder may supply
    return Layout::hitTest(m_tree, pointInLayout, actionOnly,
                            imgRes, nullptr, &outBbox);
}

}  // namespace WasabiQt
