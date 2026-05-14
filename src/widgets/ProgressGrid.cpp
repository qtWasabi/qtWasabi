// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "ProgressGrid.h"

#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/Host.h>
#include <WasabiQt/PaintCtx.h>

#include <QImage>
#include <QPainter>

namespace WasabiQt {

void ProgressGridWidget::paint(QPainter *p, PaintCtx &ctx,
                                const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;
    double frac = 0.0;
    if (ctx.host) {
        const double pos = ctx.host->sliderPosition(QStringLiteral("SEEK"));
        if (pos >= 0.0) frac = qBound(0.0, pos, 1.0);
    }
    const int filled = int(r.width() * frac);
    if (filled <= 0) return;
    // Prefer `middle` — that's the repeating fill — falling back to
    // `image` if the skin author used the simpler form.
    QString midId = attrs.value(QStringLiteral("middle"));
    if (midId.isEmpty()) midId = attrs.value(QStringLiteral("image"));
    if (midId.isEmpty()) return;
    QImage src = ctx.bmp->imageFor(midId);
    if (src.isNull()) return;
    const int srcH = qMin(r.height(), src.height());
    int drawn = 0;
    while (drawn < filled) {
        const int chunk = qMin(src.width(), filled - drawn);
        p->drawImage(QRect(r.x() + drawn, r.y(), chunk, srcH),
                     src, QRect(0, 0, chunk, srcH));
        drawn += chunk;
    }
}

}  // namespace WasabiQt
