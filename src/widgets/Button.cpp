// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Button.h"

#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/LayerPainter.h>
#include <WasabiQt/PaintCtx.h>

#include <QPainter>

namespace WasabiQt {

void ButtonWidget::paint(QPainter *p, PaintCtx &ctx,
                          const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    LayerPainter::paintLayer(p, *ctx.bmp, attrs, canvas);
}

void NStatesButtonWidget::paint(QPainter *p, PaintCtx &ctx,
                                 const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    QHash<QString, QString> a = attrs;
    QString img = a.value(QStringLiteral("image"));
    if (!img.isEmpty() && !ctx.bmp->find(img))
        img += QStringLiteral("0");
    a.insert(QStringLiteral("image"), img);
    LayerPainter::paintLayer(p, *ctx.bmp, a, canvas);
}

}  // namespace WasabiQt
