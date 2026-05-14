// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "RadioGroup.h"
#include <WasabiQt/PaintCtx.h>
#include <QPainter>

namespace WasabiQt {

void RadioGroupWidget::paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() > 0 && r.height() > 0) {
        p->save();
        p->setPen(QColor(110, 130, 160, 180));
        p->drawRect(r.adjusted(0, 0, -1, -1));
        p->restore();
    }
    for (const auto &child : children)
        if (child) child->paint(p, ctx, canvas);
}

}  // namespace WasabiQt
