// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Button.h"

#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/LayerPainter.h>
#include <WasabiQt/PaintCtx.h>

#include <QPainter>

namespace WasabiQt {

QString ButtonWidget::currentImageAttr() const {
    // Canonical Wasabi precedence (buttwnd.cpp:353-356):
    //   down > hover > [active] > normal
    // ButtonWidget (non-toggle) skips active.  Fall through to the
    // next-lower priority when the selected slot is absent so a skin
    // that only ships `image=` still renders correctly.
    //
    // SkinXml lowercases every attr name at parse time, so the keys
    // are `downimage` / `hoverimage` / `image`, not the mixed-case
    // forms the XML source uses.
    if (m_pressed && attrs.contains(QStringLiteral("downimage")))
        return QStringLiteral("downimage");
    if (m_hover && attrs.contains(QStringLiteral("hoverimage")))
        return QStringLiteral("hoverimage");
    return QStringLiteral("image");
}

void ButtonWidget::paint(QPainter *p, PaintCtx &ctx,
                          const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QString slot = currentImageAttr();
    if (slot == QStringLiteral("image")) {
        LayerPainter::paintLayer(p, *ctx.bmp, attrs, canvas);
    } else {
        QHash<QString, QString> a = attrs;
        a.insert(QStringLiteral("image"), a.value(slot));
        LayerPainter::paintLayer(p, *ctx.bmp, a, canvas);
    }
}

void ButtonWidget::onLeftButtonDown(QPoint, PaintCtx &) {
    if (!m_pressed) { m_pressed = true; requestRepaint(); }
}
void ButtonWidget::onLeftButtonUp(QPoint, PaintCtx &) {
    if (m_pressed) { m_pressed = false; requestRepaint(); }
}
void ButtonWidget::onMouseMove(QPoint, PaintCtx &) {
    if (!m_hover) { m_hover = true; requestRepaint(); }
}
void ButtonWidget::onMouseLeave(PaintCtx &) {
    if (m_hover || m_pressed) {
        m_hover = false;
        m_pressed = false;
        requestRepaint();
    }
}

// ── ToggleButton ─────────────────────────────────────────────────

QString ToggleButtonWidget::currentImageAttr() const {
    if (m_pressed && attrs.contains(QStringLiteral("downimage")))
        return QStringLiteral("downimage");
    if (m_hover && attrs.contains(QStringLiteral("hoverimage")))
        return QStringLiteral("hoverimage");
    if (m_activated && attrs.contains(QStringLiteral("activeimage")))
        return QStringLiteral("activeimage");
    return QStringLiteral("image");
}

void ToggleButtonWidget::setXmlParam(const QString &name,
                                      const QString &value) {
    // Shadow `activated` writes onto the typed bool so paint()
    // doesn't need to re-parse the string on every frame.  All
    // other attrs go through the default Widget::setXmlParam path
    // (writes to the attrs hash + triggers repaint).
    if (name.compare(QStringLiteral("activated"),
                     Qt::CaseInsensitive) == 0) {
        const bool newVal = (value == QStringLiteral("1") ||
                             value.compare(QStringLiteral("true"),
                                            Qt::CaseInsensitive) == 0);
        if (m_activated != newVal) {
            m_activated = newVal;
            requestRepaint();
        }
    }
    Widget::setXmlParam(name, value);
}

void ToggleButtonWidget::onLeftButtonUp(QPoint p, PaintCtx &ctx) {
    ButtonWidget::onLeftButtonUp(p, ctx);
    // Toggle and persist through setXmlParam so any
    // ToggleButton::setXmlParam-listening side-effects fire too.
    setXmlParam(QStringLiteral("activated"),
        m_activated ? QStringLiteral("0") : QStringLiteral("1"));
}

// ── NStatesButton ────────────────────────────────────────────────

void NStatesButtonWidget::onLeftButtonUp(QPoint p, PaintCtx &ctx) {
    ButtonWidget::onLeftButtonUp(p, ctx);
    const int n = attrs.value(QStringLiteral("nstates")).toInt();
    if (n <= 0) return;
    m_state = (m_state + 1) % n;
    requestRepaint();
}

// ── NStatesButton ────────────────────────────────────────────────

void NStatesButtonWidget::paint(QPainter *p, PaintCtx &ctx,
                                 const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    // Pick the state slot first (down / hover / normal), then apply
    // NStates' image-id suffix fallback to the chosen base.  Wasabi
    // convention: when the bare `image` id isn't a registered bitmap,
    // try `imageN` where N is the current state index (0..nstates-1).
    QHash<QString, QString> a = attrs;
    QString img = a.value(currentImageAttr());
    if (!img.isEmpty() && !ctx.bmp->find(img)) {
        const QString suffixed = img + QString::number(m_state);
        if (ctx.bmp->find(suffixed)) img = suffixed;
        else                          img += QStringLiteral("0");
    }
    a.insert(QStringLiteral("image"), img);
    LayerPainter::paintLayer(p, *ctx.bmp, a, canvas);
}

}  // namespace WasabiQt
