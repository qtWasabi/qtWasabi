// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Edit.h"

#include <qtWasabi/PaintCtx.h>

#include <QColor>
#include <QFont>
#include <QPainter>

namespace qtWasabi {

void EditWidget::paint(QPainter *p, PaintCtx &,
                        const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;
    p->fillRect(r, QColor(20, 30, 60));
    p->setPen(QColor(120, 130, 160));
    p->drawRect(r.adjusted(0, 0, -1, -1));
    QString s = attrs.value(QStringLiteral("text"));
    if (s.isEmpty()) s = attrs.value(QStringLiteral("default"));
    if (s.isEmpty()) return;
    QFont qf(QStringLiteral("sans-serif"));
    qf.setPixelSize(qMax(8, r.height() - 4));
    p->save();
    p->setFont(qf);
    p->setPen(QColor(220, 225, 235));
    p->drawText(r.adjusted(4, 0, -4, 0),
                Qt::AlignVCenter | Qt::AlignLeft, s);
    p->restore();
}

}  // namespace qtWasabi
