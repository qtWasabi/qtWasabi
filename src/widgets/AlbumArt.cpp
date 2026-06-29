// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "AlbumArt.h"

#include <qtWasabi/BitmapRegistry.h>
#include <qtWasabi/Host.h>
#include <qtWasabi/PaintCtx.h>

#include <QColor>
#include <QImage>
#include <QPainter>

namespace qtWasabi {

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
    if (qEnvironmentVariableIntValue("WASABIQT_TRACE_ALBUMART") == 1) {
        fprintf(stderr, "[albumart id=%s] r=%dx%d@(%d,%d) art=%s(%dx%d) notfound=%s\n",
                id.toLocal8Bit().constData(), w, h, r.x(), r.y(),
                art.isNull() ? "NULL" : "ok", art.width(), art.height(),
                attrs.value(QStringLiteral("notfoundimage"))
                    .toLocal8Bit().constData());
    }
    if (art.isNull()) {
        // Try the skin-declared `notfoundImage` bitmap before falling
        // back to dark grey.  Wasabi convention: AlbumArt widgets
        // ship a fallback bitmap (e.g. Bento's
        // `winamp.cover.notfound.84` showing a WINAMP wordmark) for
        // tracks without embedded cover art.
        const QString nfId =
            attrs.value(QStringLiteral("notfoundimage"));
        QImage nf;
        if (!nfId.isEmpty() && ctx.bmp) nf = ctx.bmp->imageFor(nfId);
        if (!nf.isNull()) {
            // Constrain to a 1:1 SQUARE region (min side) and centre it —
            // album covers are square; a landscape holder must not stretch
            // or wide-fit them.
            const int sq = qMin(w, h);
            const QImage scaled = nf.scaled(
                sq, sq, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const int dx = r.x() + (w - scaled.width())  / 2;
            const int dy = r.y() + (h - scaled.height()) / 2;
            p->drawImage(dx, dy, scaled);
        } else {
            p->save();
            p->fillRect(QRect(r.x(), r.y(), w, h), QColor(40, 40, 48));
            p->restore();
        }
    } else {
        // Fit the cover as a centred 1:1 SQUARE WITHIN the holder (never
        // larger than min(w,h), so it can't overflow the box); a square
        // cover fills the box's short side, centred on the long side.
        const int sq = qMin(w, h);
        const QImage scaled = art.scaled(
            sq, sq, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const int dx = r.x() + (w - scaled.width())  / 2;
        const int dy = r.y() + (h - scaled.height()) / 2;
        p->drawImage(dx, dy, scaled);
    }
    for (const auto &child : children)
        if (child) child->paint(p, ctx, canvas);
}

}  // namespace qtWasabi
