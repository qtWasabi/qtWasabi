// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Grid.h"

#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/PaintCtx.h>

#include <QImage>
#include <QPainter>

namespace WasabiQt {

void GridWidget::paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;
    QImage leftPm   = ctx.bmp->imageFor(attrs.value(QStringLiteral("left")));
    QImage middlePm = ctx.bmp->imageFor(attrs.value(QStringLiteral("middle")));
    QImage rightPm  = ctx.bmp->imageFor(attrs.value(QStringLiteral("right")));
    const int leftW  = leftPm.isNull()  ? 0 : leftPm.width();
    const int rightW = rightPm.isNull() ? 0 : rightPm.width();
    const int rh = qMin(r.height(),
                        !middlePm.isNull() ? middlePm.height()
                        : !leftPm.isNull() ? leftPm.height()
                        : r.height());
    if (!leftPm.isNull())
        p->drawImage(QRect(r.x(), r.y(), qMin(leftW, r.width()), rh),
                     leftPm,
                     QRect(0, 0, qMin(leftW, r.width()), rh));
    if (!rightPm.isNull() && r.width() > leftW)
        p->drawImage(QRect(r.x() + r.width() - rightW, r.y(),
                            qMin(rightW, r.width()), rh),
                     rightPm,
                     QRect(0, 0, qMin(rightW, r.width()), rh));
    if (!middlePm.isNull() && r.width() > leftW + rightW) {
        int mx = r.x() + leftW;
        const int mEnd = r.x() + r.width() - rightW;
        const int mw = middlePm.width();
        while (mx < mEnd) {
            const int chunk = qMin(mw, mEnd - mx);
            p->drawImage(QRect(mx, r.y(), chunk, rh),
                         middlePm,
                         QRect(0, 0, chunk, rh));
            mx += chunk;
        }
    }
}

}  // namespace WasabiQt
