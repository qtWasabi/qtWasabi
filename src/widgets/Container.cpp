// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Container.h"

#include <WasabiQt/PaintCtx.h>

#include <QPainter>

namespace WasabiQt {

void ContainerWidget::paint(QPainter *p, PaintCtx &ctx,
                             const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    // A container is *collapsed* when it declares a size attr
    // (w/h, possibly with relatw/relath) that resolves to <= 0.
    // Example: AVSGroup at the end of the video/vis drawer's close
    // tween — relath=1 h=-280 with m_nativeSize.h=280 ⇒ h=0.
    // Painting its children with the parent's canvas would inflate
    // fitparent fills (video.group's black-rect background) back
    // to the full layout area — the "drawer suddenly reappears
    // behind the player" glitch right before the drawer closes.
    //
    // Containers that DON'T declare a size attr (most groupdefs —
    // they inherit from the canvas) continue to paint children with
    // the parent's canvas as before.  <layout> is the root and never
    // declares its own size, so it's exempt.
    const bool isLayout = (tag == QStringLiteral("layout"));
    const bool hasH = !isLayout &&
        (attrs.contains(QStringLiteral("h")) ||
         attrs.contains(QStringLiteral("relath")));
    const bool hasW = !isLayout &&
        (attrs.contains(QStringLiteral("w")) ||
         attrs.contains(QStringLiteral("relatw")));
    if ((hasH && r.height() <= 0) ||
        (hasW && r.width()  <= 0))
        return;
    QSize childSize = canvas;
    if (r.width()  > 0) childSize.setWidth (r.width());
    if (r.height() > 0) childSize.setHeight(r.height());
    // Clip children to the container's rect when it declares an
    // explicit size — prevents child bitmaps drawn at natural height
    // (e.g. AVSGroup's top-edge chrome at AVSGroup.h=1 would otherwise
    // paint a full 18 px tall bitmap straight up into the titlebar
    // area mid-tween).  Containers without an explicit size aren't
    // clipped (they cover their canvas).
    const bool clipToContainer = (hasH && r.height() > 0) ||
                                 (hasW && r.width()  > 0);
    const bool translate = (r.x() != 0 || r.y() != 0) && !isLayout;
    const QPoint scroll = containerScrollOffset();
    if (translate || clipToContainer) p->save();
    if (translate) p->translate(r.x(), r.y());
    if (clipToContainer) {
        // After translate, local-coord clip rect starts at (0,0).
        p->setClipRect(QRect(0, 0,
            hasW ? r.width()  : canvas.width(),
            hasH ? r.height() : canvas.height()),
            Qt::IntersectClip);
    }
    if (scroll.x() || scroll.y()) p->translate(-scroll.x(), -scroll.y());
    for (const auto &child : children)
        if (child) child->paint(p, ctx, childSize);
    if (translate || clipToContainer) p->restore();
}

}  // namespace WasabiQt
