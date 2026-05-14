// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "AlbumArt.h"

#include <WasabiQt/Host.h>
#include <WasabiQt/PaintCtx.h>

#include <QColor>
#include <QImage>
#include <QPainter>

namespace WasabiQt {

void AlbumArtWidget::paint(QPainter *p, PaintCtx &ctx,
                            const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    int w = r.width(), h = r.height();
    if (w <= 0 || h <= 0) {
        // <AlbumArt> commonly sizes itself off the parent canvas via
        // relat*/no-explicit-size; fall through silently when we
        // can't determine bounds.
        for (const auto &child : children)
            if (child) child->paint(p, ctx, canvas);
        return;
    }
    QImage art;
    if (ctx.host) art = ctx.host->albumArt();
    if (art.isNull()) {
        p->save();
        p->fillRect(QRect(r.x(), r.y(), w, h), QColor(40, 40, 48));
        p->restore();
    } else {
        const QImage scaled = art.scaled(
            w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const int dx = r.x() + (w - scaled.width())  / 2;
        const int dy = r.y() + (h - scaled.height()) / 2;
        p->drawImage(dx, dy, scaled);
    }
    for (const auto &child : children)
        if (child) child->paint(p, ctx, canvas);
}

}  // namespace WasabiQt
