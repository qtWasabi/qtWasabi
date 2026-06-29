// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Vis.h"

#include <qtWasabi/ColorRegistry.h>
#include <qtWasabi/GammasetRegistry.h>
#include <qtWasabi/Host.h>
#include <qtWasabi/PaintCtx.h>

#include <QColor>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>

#include <cmath>

namespace qtWasabi {

void VisWidget::paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;

    // Wasabi's `<vis flipv="1">` / `<vis fliph="1">` — used by every
    // skin that paints a mirror reflection visualizer below the main
    // one (Bento's `main.vis.mirror`, Big Bento's same, WACUP-stock's
    // optional reflection, …) plus the Ctrl+Alt+Shift "flip" shortcut
    // that visualizer.maki binds at runtime.  Apply the transform
    // around the widget rect's centre so the bars draw in flipped
    // orientation without us having to invert every per-band y math
    // below.  Skin-agnostic — any `<vis>` instance with the attr
    // gets it.
    const bool flipV =
        attrs.value(QStringLiteral("flipv")) == QStringLiteral("1");
    const bool flipH =
        attrs.value(QStringLiteral("fliph")) == QStringLiteral("1");
    const bool flipped = flipV || flipH;
    if (flipped) {
        p->save();
        const QPointF c(r.x() + r.width() / 2.0,
                         r.y() + r.height() / 2.0);
        p->translate(c);
        p->scale(flipH ? -1.0 : 1.0, flipV ? -1.0 : 1.0);
        p->translate(-c);
    }

    // Visualisation colours.  A `<vis>` declares a per-level spectrum
    // palette (colorband1..16 from bottom to top, plus colorbandpeak for
    // the peak caps) and a 5-stop oscilloscope ramp (colorosc1..5).  Each
    // value is a LITERAL r,g,b passed through the widget's `gammagroup`
    // under the active gammaset, or a colour-registry key.  Any band a
    // skin omits falls back to colorallbands/colorallosc, then colorband1
    // — so a skin that declares only colorband1 keeps the old flat look.
    const QString gg = attrs.value(QStringLiteral("gammagroup"));
    auto xform = [&](QColor c) -> QColor {
        if (gg.isEmpty() || !ctx.gammasets) return c;
        const GammaGroup t = ctx.gammasets->transformFor(gg);
        int R = c.red(), G = c.green(), B = c.blue();
        if (t.gray == 1) { const int m = qMax(R, qMax(G, B)); R = G = B = m; }
        else if (t.gray == 2) { R = G = B = (R + G + B) / 3; }
        if (t.boost) {
            R = qMin(255, (R >> 1) + 127);
            G = qMin(255, (G >> 1) + 127);
            B = qMin(255, (B >> 1) + 127);
        }
        R = qBound(0, (R * (65535 + (t.r << 4))) >> 16, 255);
        G = qBound(0, (G * (65535 + (t.g << 4))) >> 16, 255);
        B = qBound(0, (B * (65535 + (t.b << 4))) >> 16, 255);
        return QColor(R, G, B);
    };
    auto resolveColor = [&](const QString &name, const QColor &fallback) -> QColor {
        const QString v = attrs.value(name);
        if (v.isEmpty()) return fallback;
        QColor c = fallback;
        if (v.contains(QChar(','))) {
            const auto parts = v.split(QChar(','));
            if (parts.size() == 3)
                c = QColor(parts[0].toInt(), parts[1].toInt(),
                           parts[2].toInt());
        } else if (ctx.colors) {
            c = ctx.colors->resolve(v, ctx.gammasets, fallback);
        }
        return xform(c);
    };
    const QColor band =
        resolveColor(QStringLiteral("colorband1"), QColor(255, 255, 255));
    const QColor allBands = resolveColor(QStringLiteral("colorallbands"), band);
    QColor bandColors[16];
    for (int i = 0; i < 16; ++i)
        bandColors[i] =
            resolveColor(QStringLiteral("colorband%1").arg(i + 1), allBands);
    const QColor peakColor =
        resolveColor(QStringLiteral("colorbandpeak"), band);
    const QColor allOsc = resolveColor(QStringLiteral("colorallosc"), band);
    QColor oscColors[5];
    for (int i = 0; i < 5; ++i)
        oscColors[i] =
            resolveColor(QStringLiteral("colorosc%1").arg(i + 1), allOsc);
    // Vertical spectrum gradient (band1 at the bottom → band16 at the top)
    // and oscilloscope ramp (osc1 top → osc5 bottom), in painter space so
    // each bar / polyline segment is coloured by its position.
    QLinearGradient barGrad(0, r.y() + r.height(), 0, r.y());
    for (int i = 0; i < 16; ++i)
        barGrad.setColorAt(i / 15.0, bandColors[i]);
    const QBrush barBrush(barGrad);
    switch (ctx.visMode) {
    case 0:  // Off
        break;
    case 1: {  // Spectrum analyzer — 19 log-scaled bands from FFT
        const float *spec  = ctx.host ? ctx.host->spectrumData() : nullptr;
        const float *peaks = ctx.host ? ctx.host->peakData()     : nullptr;
        const bool showPeaks = ctx.host && ctx.host->peaksVisible();
        if (!spec) break;
        const int bands = 19;
        const int maxH  = qMax(1, r.height() - 1);
        // Position each bar's left/right edge proportionally to the
        // widget rect.  Integer-divide r.width()/bands rounds DOWN
        // (e.g. 72/19=3) which left 15 px of dead space on the
        // right; the proportional formula distributes that across
        // all bars so the strip spans the full widget width.
        for (int i = 0; i < bands; ++i) {
            const int xL = r.x() + (i     * r.width()) / bands;
            const int xR = r.x() + ((i+1) * r.width()) / bands;
            const int barW = qMax(1, xR - xL - 1);
            const int h  = qMax(0, int(spec[i] * maxH));
            if (h > 0) {
                p->fillRect(xL, r.y() + (r.height() - h),
                            barW, h, barBrush);
            }
            // Peak dot — 1-pixel-tall cap at the floating peak.
            if (showPeaks && peaks) {
                const int peakH = int(peaks[i] * maxH);
                if (peakH > 0)
                    p->fillRect(xL,
                                r.y() + (r.height() - peakH - 1),
                                barW, 1, peakColor);
            }
        }
        break;
    }
    case 2: {  // Oscilloscope — polyline of 75 PCM samples
        const float *osc = ctx.host ? ctx.host->oscData() : nullptr;
        if (!osc) break;
        p->save();
        QLinearGradient oscGrad(0, r.y(), 0, r.y() + r.height());
        for (int i = 0; i < 5; ++i)
            oscGrad.setColorAt(i / 4.0, oscColors[i]);
        QPen pen(QBrush(oscGrad), 1);
        p->setPen(pen);
        const int samples = 75;
        const int mid = r.y() + r.height() / 2;
        const double amp = (r.height() / 2.0 - 1.0);
        QPoint prev(r.x(),
                    mid + int(qBound(-1.0f, osc[0], 1.0f) * amp));
        const double xStep =
            samples > 1 ? double(r.width()) / (samples - 1) : 0.0;
        for (int i = 1; i < samples; ++i) {
            const int x = r.x() + int(i * xStep);
            const int y = mid +
                int(qBound(-1.0f, osc[i], 1.0f) * amp);
            const QPoint cur(x, y);
            p->drawLine(prev, cur);
            prev = cur;
        }
        p->restore();
        break;
    }
    case 3: {  // VU meter — two horizontal L/R bars
        if (!ctx.host) break;
        const float l = qBound(0.0f, ctx.host->vuLeft(),  1.0f);
        const float rch = qBound(0.0f, ctx.host->vuRight(), 1.0f);
        const int half = r.height() / 2;
        p->fillRect(r.x(), r.y() + 1,
                    int(r.width() * l),   half - 2, band);
        p->fillRect(r.x(), r.y() + half + 1,
                    int(r.width() * rch), half - 2, band);
        break;
    }
    }
    if (flipped) p->restore();
}

}  // namespace qtWasabi
