// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "DropDownList.h"
#include <WasabiQt/PaintCtx.h>
#include <QPainter>
#include <QPolygon>

namespace WasabiQt {

void DropDownListWidget::paint(QPainter *p, PaintCtx &, const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;
    p->save();
    p->fillRect(r, QColor(28, 36, 50));
    p->setPen(QColor(140, 150, 170));
    p->drawRect(r.adjusted(0, 0, -1, -1));
    // Down-pointing triangle on the right edge.
    const int sz = qMin(8, r.height() - 4);
    const int cx = r.x() + r.width() - sz - 4;
    const int cy = r.y() + (r.height() - sz) / 2;
    QPolygon tri;
    tri << QPoint(cx,        cy)
        << QPoint(cx + sz,   cy)
        << QPoint(cx + sz/2, cy + sz);
    p->setBrush(QColor(180, 200, 230));
    p->setPen(Qt::NoPen);
    p->drawPolygon(tri);
    p->restore();
}

}  // namespace WasabiQt
