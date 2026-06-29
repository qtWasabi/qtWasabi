// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "TabSheet.h"

#include <qtWasabi/PaintCtx.h>

#include <QColor>
#include <QPainter>

namespace qtWasabi {

void TabSheetWidget::paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;
    p->save();
    // Content frame with a 14-px tab-strip reserved at the top.
    const int tabH = 14;
    p->fillRect(r, QColor(45, 55, 75));
    p->setPen(QColor(140, 150, 170));
    p->drawRect(r.adjusted(0, 0, -1, -1));
    p->fillRect(QRect(r.x() + 1, r.y() + 1, r.width() - 2, tabH - 1),
                QColor(70, 80, 100));
    p->setPen(QColor(180, 190, 210));
    p->drawLine(r.x(), r.y() + tabH,
                r.x() + r.width() - 1, r.y() + tabH);
    p->restore();
    // Recurse children inside the content area (below the tab strip).
    QSize childCanvas(r.width(), qMax(0, r.height() - tabH));
    for (const auto &child : children)
        if (child) child->paint(p, ctx, childCanvas);
}

}  // namespace qtWasabi
