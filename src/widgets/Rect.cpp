// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Rect.h"

#include <qtWasabi/ColorRegistry.h>
#include <qtWasabi/PaintCtx.h>

#include <QColor>
#include <QPainter>

namespace qtWasabi {

void RectWidget::paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;
    QColor c(0, 0, 0);
    const QString col = attrs.value(QStringLiteral("color"));
    if (col.contains(QChar(','))) {
        const auto parts = col.split(QChar(','));
        if (parts.size() == 3)
            c = QColor(parts[0].toInt(), parts[1].toInt(),
                       parts[2].toInt());
    } else if (ctx.colors) {
        c = ctx.colors->resolve(col, ctx.gammasets, c);
    }
    if (attrs.value(QStringLiteral("filled")) == QStringLiteral("0")) {
        p->setPen(c);
        p->drawRect(r);
    } else {
        p->fillRect(r, c);
    }
}

}  // namespace qtWasabi
