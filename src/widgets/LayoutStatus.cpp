// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "LayoutStatus.h"

#include <qtWasabi/PaintCtx.h>

#include <QColor>
#include <QPainter>

namespace qtWasabi {

void LayoutStatusWidget::paint(QPainter *p, PaintCtx &, const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    if (attrs.value(QStringLiteral("bg")) == QStringLiteral("0"))
        return;  // skin opted out of the bg fill (used by sysmenu.status).
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;
    p->save();
    p->fillRect(r, QColor(60, 70, 90, 200));
    p->setPen(QColor(140, 150, 170));
    p->drawRect(r.adjusted(0, 0, -1, -1));
    p->restore();
}

}  // namespace qtWasabi
