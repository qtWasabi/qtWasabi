// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "ComponentBucket.h"

namespace WasabiQt {

QPoint ComponentBucketWidget::containerScrollOffset() const {
    const int scroll = attrs.value(QStringLiteral("_scroll")).toInt();
    const int step   = attrs.value(QStringLiteral("_entry_step")).toInt();
    if (scroll <= 0 || step <= 0) return QPoint(0, 0);
    const bool vertical = attrs.value(QStringLiteral("vertical")) ==
                          QStringLiteral("1");
    return vertical ? QPoint(0, scroll * step)
                    : QPoint(scroll * step, 0);
}

}  // namespace WasabiQt
