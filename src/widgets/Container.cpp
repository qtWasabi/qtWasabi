// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Container.h"

#include <qtWasabi/CfgAttribStore.h>
#include <qtWasabi/HitCtx.h>
#include <qtWasabi/PaintCtx.h>

#include <QPainter>

namespace qtWasabi {

ContainerWidget::~ContainerWidget() {
    if (m_tabSubHandle)
        CfgAttribStore::instance().unsubscribe(m_tabSubHandle);
}

void ContainerWidget::onAttrsInitialized() {
    const QString tabKey = attrs.value(QStringLiteral("_tab_key"));
    if (tabKey.isEmpty()) return;
    bool ok = false;
    const int tabVal = attrs.value(
        QStringLiteral("_tab_value")).toInt(&ok);
    if (!ok) return;
    m_tabKey   = tabKey;
    m_tabValue = tabVal;
    if (m_tabSubHandle) {
        CfgAttribStore::instance().unsubscribe(m_tabSubHandle);
        m_tabSubHandle = 0;
    }
    auto applyVisibility = [this](int v) {
        const QString want = (v == m_tabValue) ? QStringLiteral("1")
                                                : QStringLiteral("0");
        if (attrs.value(QStringLiteral("visible")) != want) {
            attrs.insert(QStringLiteral("visible"), want);
            requestRepaint();
        }
    };
    auto &store = CfgAttribStore::instance();
    m_tabSubHandle = store.subscribe(m_tabKey, applyVisibility);
    applyVisibility(store.get(m_tabKey));
}

void ContainerWidget::paint(QPainter *p, PaintCtx &ctx,
                             const QSize &canvas) {
    if (qEnvironmentVariableIntValue("WASABIQT_TRACE_CONTAINER") == 1 &&
        (id.startsWith(QLatin1String("switch.")) ||
         id == QStringLiteral("sui.content") ||
         id == QStringLiteral("player.mainframe") ||
         id == QStringLiteral("player.dualwnd.pl.info") ||
         id == QStringLiteral("player.dualwnd") ||
         id == QStringLiteral("player.component.playlist.frame") ||
         id == QStringLiteral("playlist.dualwnd") ||
         id == QStringLiteral("player.component.playlist") ||
         id == QStringLiteral("playlistpro"))) {
        fprintf(stderr, "[container id=%s] canvas=%dx%d r=%dx%d h=%s relath=%s\n",
                id.toLocal8Bit().constData(),
                canvas.width(), canvas.height(),
                resolveRect(canvas).width(), resolveRect(canvas).height(),
                attrs.value(QStringLiteral("h")).toLocal8Bit().constData(),
                attrs.value(QStringLiteral("relath")).toLocal8Bit().constData());
    }
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

Widget *ContainerWidget::hitTest(QPoint point, QPoint origin,
                                  const QSize &canvas,
                                  HitCtx &ctx, QRect *outBbox) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return nullptr;
    // Containers that declared an explicit size clip their hit-test
    // recursion to that rect — mirror of paint()'s `clipToContainer`
    // setClipRect.  Without this, scrolled-off componentbucket
    // entries (visually clipped) still consume clicks at coords that
    // visibly belong to widgets BELOW the bucket.
    const bool isLayout = (tag == QStringLiteral("layout"));
    if (!isLayout) {
        const QRect r = resolveRect(canvas);
        const bool hasH =
            attrs.contains(QStringLiteral("h")) ||
            attrs.contains(QStringLiteral("relath"));
        const bool hasW =
            attrs.contains(QStringLiteral("w")) ||
            attrs.contains(QStringLiteral("relatw"));
        if ((hasW && r.width()  > 0) || (hasH && r.height() > 0)) {
            const int ox = origin.x() + r.x();
            const int oy = origin.y() + r.y();
            const QRect bounds(
                ox, oy,
                hasW ? r.width()  : canvas.width(),
                hasH ? r.height() : canvas.height());
            if (!bounds.contains(point)) return nullptr;
        }
    }
    return Widget::hitTest(point, origin, canvas, ctx, outBbox);
}

}  // namespace qtWasabi
