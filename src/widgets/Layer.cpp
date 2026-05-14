// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Layer.h"

#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/GammasetRegistry.h>
#include <WasabiQt/Host.h>
#include <WasabiQt/LayerPainter.h>
#include <WasabiQt/PaintCtx.h>

#include <QPainter>

#include <cstdio>
#include <cstdlib>

namespace WasabiQt {

void LayerWidget::paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;

    // 1. Colour-themes scrollbar thumb: track the list's topRow.
    if (attrs.value(QStringLiteral("image")) ==
            QStringLiteral("wasabi.scrollbar.vertical.button") &&
        ctx.gammasets && ctx.colorthemesBboxOut) {
        QHash<QString, QString> a = attrs;
        const int names = ctx.gammasets->names().size();
        const int rowsVisible = 8;          // 80 px viewport / 10 px-per-row
        // Track between top arrow (y=5..22) and bottom arrow (y=68..85):
        // thumb's valid y range is 22..68-31 = 22..37.  Thumb bitmap 13×31.
        const int trackTop = 22;
        const int trackBot = 68;
        const int thumbH   = 31;
        const int travel   = qMax(0, (trackBot - trackTop) - thumbH);
        int top = ctx.colorthemesTopRow;
        const int maxTop = qMax(0, names - rowsVisible);
        if (top > maxTop) top = maxTop;
        if (top < 0)      top = 0;
        const double frac = (maxTop > 0)
                          ? double(top) / double(maxTop)
                          : 0.0;
        const int newY = trackTop + int(frac * travel);
        a.insert(QStringLiteral("y"), QString::number(newY));
        LayerPainter::paintLayer(p, *ctx.bmp, a, canvas);
        return;
    }

    // 2. Mono/stereo lit indicator (monoster.maki replacement).
    if (ctx.host &&
        (id == QStringLiteral("mono") ||
         id == QStringLiteral("stereo"))) {
        const int ch = ctx.host->channelCount();
        const bool active = (id == QStringLiteral("mono")  && ch == 1) ||
                            (id == QStringLiteral("stereo") && ch >= 2);
        if (active) {
            QString img = attrs.value(QStringLiteral("image"));
            const QString lit = img.endsWith(QStringLiteral(".inactive"))
                ? img.chopped(9) + QStringLiteral(".active")
                : img + QStringLiteral(".active");
            if (ctx.bmp->find(lit)) {
                QHash<QString, QString> a = attrs;
                a.insert(QStringLiteral("image"), lit);
                LayerPainter::paintLayer(p, *ctx.bmp, a, canvas);
                return;
            }
        }
    }

    // 3. Sysregion cutout-mask layers: region-only, no visible blit.
    const QString sr = attrs.value(QStringLiteral("sysregion"));
    if (!sr.isEmpty() && sr.startsWith(QChar('-')))
        return;

    // 4. Volumebar live width: derive from host volume position.
    //
    // Geometry (player-normal Modern):
    //   Volume slider: x=183, w=86; thumb 21 px wide; travel = 65.
    //   Volumebar layer:  x=185, so 2 px right of slider's left.
    //   Thumb's centre at x = slider.x + pos*travel + thumb.w/2
    //                      = 183 + 65*pos + 10
    //   Volumebar.w = thumb_centre - volumebar.x
    //               = (183 + 65*pos + 10) - 185
    //               = 65*pos + 8
    if (id == QStringLiteral("volumebar") && ctx.host) {
        const double vol = ctx.host->sliderPosition(
            QStringLiteral("VOLUME"));
        if (vol >= 0.0) {
            QHash<QString, QString> a = attrs;
            const int travel = 65;
            const int offset = 8;
            const int newW = qMax(1, int(vol * travel + offset));
            a.insert(QStringLiteral("w"), QString::number(newW));
            if (qEnvironmentVariableIntValue("WASABIQT_TRACE_VOL") == 1) {
                std::fprintf(stderr,
                    "[volumebar] vol=%.3f w=%d xform=(%g,%g)\n",
                    vol, newW,
                    p->transform().dx(), p->transform().dy());
                std::fflush(stderr);
            }
            LayerPainter::paintLayer(p, *ctx.bmp, a, canvas);
            return;
        }
    }

    LayerPainter::paintLayer(p, *ctx.bmp, attrs, canvas);
}

}  // namespace WasabiQt
