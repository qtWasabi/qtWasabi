// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "WindowHolder.h"

#include <WasabiQt/PaintCtx.h>

#include <QColor>
#include <QPainter>

namespace WasabiQt {

void WindowHolderWidget::paint(QPainter *p, PaintCtx &,
                                const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() > 0 && r.height() > 0)
        p->fillRect(r, QColor(0, 0, 0));
}

}  // namespace WasabiQt
