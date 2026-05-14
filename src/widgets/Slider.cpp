// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Slider.h"

#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/Host.h>
#include <WasabiQt/LayerPainter.h>
#include <WasabiQt/PaintCtx.h>

#include <QImage>
#include <QPainter>

namespace WasabiQt {

void SliderWidget::paint(QPainter *p, PaintCtx &ctx,
                          const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;
    // Optional background bitmap (most Modern-skin sliders paint
    // their groove via separate <layer> siblings; some do carry an
    // `image=` attr though).
    const QString bgId = attrs.value(QStringLiteral("image"));
    if (!bgId.isEmpty()) {
        QHash<QString, QString> bg = attrs;
        bg.remove(QStringLiteral("thumb"));
        LayerPainter::paintLayer(p, *ctx.bmp, bg, canvas);
    }
    const QString thumbId = attrs.value(QStringLiteral("thumb"));
    if (thumbId.isEmpty()) return;
    const QString action = attrs.value(QStringLiteral("action"));
    // Default to a sensible resting position for actions the host
    // doesn't recognise (most commonly EQ_BAND with a per-band
    // `param=`, which neither the default Host nor QtampHost expose
    // yet).  Centred = no boost / no cut for EQ; most other unknown
    // sliders look reasonable centred too.  Without this the thumb
    // never paints and the EQ sliders look like inert grooves.
    double pos = 0.5;
    if (ctx.host) {
        const double live = ctx.host->sliderPosition(action);
        if (live >= 0.0) pos = live;
    }
    QImage thumb = ctx.bmp->imageFor(thumbId);
    if (thumb.isNull()) return;
    const bool vertical = attrs.value(
        QStringLiteral("orientation")).compare(
        QStringLiteral("vertical"), Qt::CaseInsensitive) == 0;
    int thumbX, thumbY;
    if (vertical) {
        // Vertical sliders run top-to-bottom: pos=0 at top, pos=1 at
        // bottom.  EQ_BAND convention is pos=0.5 = 0 dB at the centre.
        const int travel = qMax(0, r.height() - thumb.height());
        thumbX = r.x() + (r.width() - thumb.width()) / 2;
        thumbY = r.y() + int(pos * travel);
    } else {
        const int travel = qMax(0, r.width() - thumb.width());
        thumbX = r.x() + int(pos * travel);
        thumbY = r.y() + (r.height() - thumb.height()) / 2;
    }
    p->drawImage(thumbX, thumbY, thumb);
}

}  // namespace WasabiQt
