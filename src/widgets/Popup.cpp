// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Popup.h"
#include <qtWasabi/PaintCtx.h>
#include <QPainter>

namespace qtWasabi {

void PopupWidget::paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;
    p->save();
    p->fillRect(r, QColor(20, 28, 44, 230));
    p->setPen(QColor(140, 150, 170));
    p->drawRect(r.adjusted(0, 0, -1, -1));
    p->restore();
    for (const auto &child : children)
        if (child) child->paint(p, ctx, QSize(r.width(), r.height()));
}

}  // namespace qtWasabi
