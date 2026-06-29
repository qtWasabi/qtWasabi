// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "CheckBox.h"

#include <qtWasabi/PaintCtx.h>

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPen>

namespace qtWasabi {

void CheckBoxWidget::paint(QPainter *p, PaintCtx &, const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;

    // 11×11 box left-aligned, vertically centred.
    const int boxSize = qMin(11, qMin(r.height() - 2, 11));
    const QRect box(r.x() + 1,
                    r.y() + (r.height() - boxSize) / 2,
                    boxSize, boxSize);
    p->save();
    p->fillRect(box, QColor(40, 50, 70));
    p->setPen(QColor(160, 170, 200));
    p->drawRect(box.adjusted(0, 0, -1, -1));
    if (m_checked) {
        QPen pen(QColor(220, 235, 255));
        pen.setWidth(2);
        p->setPen(pen);
        // Tick mark: two line segments forming a check.
        const int x1 = box.x() + 2;
        const int y1 = box.y() + boxSize / 2;
        const int x2 = box.x() + boxSize / 2 - 1;
        const int y2 = box.y() + boxSize - 3;
        const int x3 = box.x() + boxSize - 2;
        const int y3 = box.y() + 2;
        p->drawLine(x1, y1, x2, y2);
        p->drawLine(x2, y2, x3, y3);
    }
    // Label to the right of the box.
    const QString label = attrs.value(QStringLiteral("text"));
    if (!label.isEmpty()) {
        QFont f(QStringLiteral("sans-serif"));
        f.setPixelSize(qMax(9, boxSize));
        p->setFont(f);
        p->setPen(QColor(220, 225, 235));
        p->drawText(QRect(box.right() + 4, r.y(),
                          r.width() - boxSize - 5, r.height()),
                    Qt::AlignVCenter | Qt::AlignLeft, label);
    }
    p->restore();
}

void CheckBoxWidget::onLeftButtonDown(QPoint, PaintCtx &) {
    m_checked = !m_checked;
}

}  // namespace qtWasabi
