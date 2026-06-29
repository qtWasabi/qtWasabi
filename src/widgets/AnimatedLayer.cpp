// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "AnimatedLayer.h"

#include <qtWasabi/BitmapRegistry.h>
#include <qtWasabi/LayerPainter.h>
#include <qtWasabi/PaintCtx.h>

#include <QPainter>

namespace qtWasabi {

void AnimatedLayerWidget::paint(QPainter *p, PaintCtx &ctx,
                                  const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    const QString image = attrs.value(QStringLiteral("image"));
    if (!image.isEmpty() && (r.width() > 0 || r.height() > 0)) {
        // `frameheight=` / `framewidth=` carve the bitmap into a
        // vertical / horizontal sprite strip.  Paint the single
        // `start=` frame (default 0) sliced out of the strip.
        // Without this slicing, VU AnimatedLayers (frameheight=1)
        // paint the ENTIRE source bitmap as a single image —
        // overflowing past the strip's per-frame slice and leaking
        // the rest of the VU peak ramp across the display panel.
        bool ok = false;
        const int fh = attrs.value(
            QStringLiteral("frameheight")).toInt(&ok);
        const bool hasFh = ok && fh > 0;
        const int fw = attrs.value(
            QStringLiteral("framewidth")).toInt(&ok);
        const bool hasFw = ok && fw > 0;
        if (hasFh || hasFw) {
            const int start = attrs.value(
                QStringLiteral("start")).toInt();
            QImage whole = ctx.bmp ? ctx.bmp->imageFor(image) : QImage();
            if (!whole.isNull()) {
                const int sx = hasFw ? start * fw : 0;
                const int sy = hasFh ? start * fh : 0;
                const int sw = hasFw ? fw : whole.width();
                const int sh = hasFh ? fh : whole.height();
                const QImage frame = whole.copy(sx, sy, sw, sh);
                p->drawImage(r.x(), r.y(), frame);
            }
        } else {
            LayerPainter::paintLayer(p, *ctx.bmp, attrs, canvas, ctx.windowActive);
        }
    }
    for (const auto &child : children)
        if (child) child->paint(p, ctx, canvas);
}

}  // namespace qtWasabi
