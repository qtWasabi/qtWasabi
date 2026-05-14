// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "AnimatedLayer.h"

#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/LayerPainter.h>
#include <WasabiQt/PaintCtx.h>

#include <QPainter>

namespace WasabiQt {

void AnimatedLayerWidget::paint(QPainter *p, PaintCtx &ctx,
                                  const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() > 0 || r.height() > 0 ||
        !attrs.value(QStringLiteral("image")).isEmpty()) {
        LayerPainter::paintLayer(p, *ctx.bmp, attrs, canvas);
    }
    for (const auto &child : children)
        if (child) child->paint(p, ctx, canvas);
}

}  // namespace WasabiQt
