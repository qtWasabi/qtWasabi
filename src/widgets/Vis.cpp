// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Vis.h"

#include <WasabiQt/ColorRegistry.h>
#include <WasabiQt/GammasetRegistry.h>
#include <WasabiQt/Host.h>
#include <WasabiQt/PaintCtx.h>

#include <QColor>
#include <QPainter>
#include <QPen>

#include <cmath>

namespace WasabiQt {

void VisWidget::paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;

    // Visualisation bar colour: the `<vis>` widget carries a
    // `gammagroup="DisplayVis"` attribute — the colorband colours
    // are LITERAL r,g,b values that pass through that group under
    // the active gammaset.
    const QString c1 = attrs.value(QStringLiteral("colorband1"));
    QColor band(255, 255, 255);
    if (c1.contains(QChar(','))) {
        const auto parts = c1.split(QChar(','));
        if (parts.size() == 3)
            band = QColor(parts[0].toInt(), parts[1].toInt(),
                          parts[2].toInt());
    } else if (ctx.colors) {
        band = ctx.colors->resolve(c1, ctx.gammasets, band);
    }
    const QString gg = attrs.value(QStringLiteral("gammagroup"));
    if (!gg.isEmpty() && ctx.gammasets) {
        const GammaGroup t = ctx.gammasets->transformFor(gg);
        // Same per-channel transform GammasetRegistry uses for
        // bitmaps, applied to the literal band colour.
        int R = band.red(), G = band.green(), B = band.blue();
        if (t.gray == 1) {
            const int m = qMax(R, qMax(G, B));
            R = G = B = m;
        } else if (t.gray == 2) {
            R = G = B = (R + G + B) / 3;
        }
        if (t.boost) {
            R = qMin(255, (R >> 1) + 127);
            G = qMin(255, (G >> 1) + 127);
            B = qMin(255, (B >> 1) + 127);
        }
        const int rm = 65535 + (t.r << 4);
        const int gm = 65535 + (t.g << 4);
        const int bm = 65535 + (t.b << 4);
        R = qBound(0, (R * rm) >> 16, 255);
        G = qBound(0, (G * gm) >> 16, 255);
        B = qBound(0, (B * bm) >> 16, 255);
        band = QColor(R, G, B);
    }
    const double level = ctx.host
        ? qBound(0.0, ctx.host->audioLevel() * 4.0, 1.0)
        : 0.0;
    switch (ctx.visMode) {
    case 0:  // Off
        break;
    case 1: {  // Spectrum analyzer
        const int barCount = 16;
        const int barW = r.width() / barCount;
        const int maxH = r.height() - 4;
        for (int i = 0; i < barCount; ++i) {
            const int rawH = 4 + ((i * 17 + 3) % maxH);
            const int h = qMax(1, int(rawH * level));
            p->fillRect(r.x() + i * barW + 1,
                        r.y() + (r.height() - h),
                        barW - 1, h, band);
        }
        break;
    }
    case 2: {  // Oscilloscope — pseudo-waveform line
        p->save();
        QPen pen(band); pen.setWidth(1);
        p->setPen(pen);
        const int samples = r.width();
        const int mid = r.y() + r.height() / 2;
        const double amp = (r.height() / 2.0 - 2.0) * level;
        QPoint prev(r.x(), mid);
        for (int x = 1; x < samples; ++x) {
            const double phase = x * 0.35;
            const int y = mid +
                int(std::sin(phase) * amp *
                    (0.5 + 0.5 * std::sin(x * 0.07)));
            const QPoint cur(r.x() + x, y);
            p->drawLine(prev, cur);
            prev = cur;
        }
        p->restore();
        break;
    }
    case 3: {  // VU meter — two horizontal bars (L/R)
        const int half = r.height() / 2;
        const int filledW = int(r.width() * level);
        p->fillRect(r.x(), r.y() + 1,
                    filledW, half - 2, band);
        p->fillRect(r.x(), r.y() + half + 1,
                    filledW, half - 2, band);
        break;
    }
    }
}

}  // namespace WasabiQt
