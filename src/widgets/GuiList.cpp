// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "GuiList.h"
#include <qtWasabi/PaintCtx.h>
#include <QPainter>
#include <QFont>

namespace qtWasabi {

void GuiListWidget::paint(QPainter *p, PaintCtx &, const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;
    p->save();
    p->fillRect(r, QColor(30, 40, 56));
    p->setPen(QColor(120, 140, 170));
    p->drawRect(r.adjusted(0, 0, -1, -1));
    if (r.width() > 60 && r.height() > 18) {
        QFont f(QStringLiteral("sans-serif"));
        f.setPixelSize(10);
        p->setFont(f);
        p->setPen(QColor(180, 200, 230));
        p->drawText(r, Qt::AlignCenter, QStringLiteral("List"));
    }
    p->restore();
}

}  // namespace qtWasabi
