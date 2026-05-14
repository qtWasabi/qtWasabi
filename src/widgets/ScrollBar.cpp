// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "ScrollBar.h"

#include <WasabiQt/PaintCtx.h>

#include <QColor>
#include <QPainter>

namespace WasabiQt {

void ScrollBarWidget::paint(QPainter *p, PaintCtx &, const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;

    const bool vertical = attrs.value(
        QStringLiteral("orientation")).compare(
        QStringLiteral("horizontal"), Qt::CaseInsensitive) != 0;

    p->save();
    // Track background — sunken grey.
    p->fillRect(r, QColor(48, 56, 72));
    p->setPen(QColor(140, 150, 170));
    p->drawRect(r.adjusted(0, 0, -1, -1));

    // Thumb — 1/3 of the track, parked at the top/left.
    QRect thumb;
    if (vertical) {
        const int th = qMax(8, r.height() / 3);
        thumb = QRect(r.x() + 1, r.y() + 1, r.width() - 2, th);
    } else {
        const int tw = qMax(8, r.width() / 3);
        thumb = QRect(r.x() + 1, r.y() + 1, tw, r.height() - 2);
    }
    p->fillRect(thumb, QColor(120, 130, 150));
    p->setPen(QColor(200, 210, 230));
    p->drawRect(thumb.adjusted(0, 0, -1, -1));
    p->restore();
}

}  // namespace WasabiQt
