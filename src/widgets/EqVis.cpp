// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "EqVis.h"

#include <qtWasabi/ColorRegistry.h>
#include <qtWasabi/Host.h>
#include <qtWasabi/PaintCtx.h>

#include <QColor>
#include <QPainter>

namespace qtWasabi {

void EqVisWidget::paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;

    // 10-band EQ.  Each band's position is 0..1 (slider value);
    // 0.5 = no gain (centre of the graph).  Without a host, all
    // bands sit at 0.5 — the flat baseline.
    constexpr int bands = 10;
    const int barW = qMax(1, r.width() / bands);
    const int midY = r.y() + r.height() / 2;
    const int amp  = (r.height() / 2) - 1;

    QColor band(160, 200, 255);
    if (ctx.colors) band = ctx.colors->resolve(
        QStringLiteral("Eqgraph"), ctx.gammasets, band);

    p->save();
    for (int i = 0; i < bands; ++i) {
        double pos = 0.5;
        if (ctx.host) {
            const double v = ctx.host->sliderPosition(
                QStringLiteral("EQ_BAND_") + QString::number(i));
            if (v >= 0.0) pos = v;
        }
        // Slider 0..1 → graph -amp..+amp.  0.5 == 0 dB (mid).
        const int barH = int((pos - 0.5) * 2.0 * amp);
        const int x = r.x() + i * barW;
        if (barH >= 0)
            p->fillRect(x + 1, midY - barH, barW - 1, qMax(1, barH), band);
        else
            p->fillRect(x + 1, midY,        barW - 1, qMax(1, -barH), band);
    }
    // Centre baseline.
    p->setPen(QColor(64, 64, 64, 128));
    p->drawLine(r.x(), midY, r.x() + r.width() - 1, midY);
    p->restore();
}

}  // namespace qtWasabi
