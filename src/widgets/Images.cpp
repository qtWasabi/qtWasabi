// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Images.h"

#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/Host.h>
#include <WasabiQt/PaintCtx.h>

#include <QImage>
#include <QPainter>

namespace WasabiQt {

void ImagesWidget::paint(QPainter *p, PaintCtx &ctx,
                          const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;
    QImage src = ctx.bmp->imageFor(attrs.value(QStringLiteral("images")));
    if (src.isNull()) return;
    const int stride = attrs.value(QStringLiteral("imagesspacing")).toInt();
    if (stride <= 0) return;
    const int frames = src.height() / stride;
    if (frames <= 0) return;
    // Resolve the source value.  Wasabi's `source=` names the value
    // the widget is bound to.  We support the two classic bindings;
    // everything else falls back to the middle frame.
    double v = 0.5;
    const QString src_ = attrs.value(QStringLiteral("source")).toLower();
    if (ctx.host) {
        if (src_ == QStringLiteral("volume")) {
            double pos = ctx.host->sliderPosition(QStringLiteral("VOLUME"));
            if (pos >= 0.0) v = qBound(0.0, pos, 1.0);
        } else if (src_ == QStringLiteral("balance") ||
                   src_ == QStringLiteral("pan")) {
            double pos = ctx.host->sliderPosition(QStringLiteral("PAN"));
            // Balance: -1..+1 → 0..1
            if (pos >= -1.0) v = qBound(0.0, (pos + 1.0) * 0.5, 1.0);
        }
    }
    int frame = qBound(0, int(v * (frames - 1) + 0.5), frames - 1);
    const QRect srcRect(0, frame * stride, src.width(), r.height());
    p->drawImage(r, src, srcRect);
}

}  // namespace WasabiQt
