// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Splitter.h"

#include <qtWasabi/PaintCtx.h>

#include <QColor>
#include <QPainter>

namespace qtWasabi {

void SplitterWidget::paint(QPainter *p, PaintCtx &, const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;
    // Two-tone groove: dark line + light line, mirrors Win32's
    // CTL3D bevel that Wasabi's splitter draws by default.
    p->save();
    if (r.width() >= r.height()) {
        // Horizontal splitter — divider line across width.
        const int mid = r.y() + r.height() / 2;
        p->fillRect(r.x(), mid - 1, r.width(), 1, QColor(48, 56, 72));
        p->fillRect(r.x(), mid,     r.width(), 1, QColor(140, 150, 170));
    } else {
        // Vertical splitter — divider line across height.
        const int mid = r.x() + r.width() / 2;
        p->fillRect(mid - 1, r.y(), 1, r.height(), QColor(48, 56, 72));
        p->fillRect(mid,     r.y(), 1, r.height(), QColor(140, 150, 170));
    }
    p->restore();
}

}  // namespace qtWasabi
