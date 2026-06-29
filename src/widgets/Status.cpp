// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Status.h"

#include <qtWasabi/BitmapRegistry.h>
#include <qtWasabi/Host.h>
#include <qtWasabi/LayerPainter.h>
#include <qtWasabi/PaintCtx.h>

#include <QImage>
#include <QPainter>

namespace qtWasabi {

namespace {
int firstVisibleCol(BitmapRegistry &bmp, const QString &id) {
    if (id.isEmpty()) return 0;
    QImage im = bmp.imageFor(id);
    if (im.isNull()) return 0;
    im = im.convertToFormat(QImage::Format_ARGB32);
    for (int x = 0; x < im.width(); ++x) {
        for (int y = 0; y < im.height(); ++y) {
            const QRgb px = im.pixel(x, y);
            const int r = qRed(px), g = qGreen(px), b = qBlue(px);
            const int mx = qMax(r, qMax(g, b));
            const int mn = qMin(r, qMin(g, b));
            if (qAlpha(px) > 0 && mx > 80 && (mx - mn) > 30)
                return x;
        }
    }
    return 0;
}
}  // namespace

void StatusWidget::paint(QPainter *p, PaintCtx &ctx,
                          const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    if (!ctx.host) return;
    // SkinXml lowercases attr names; look up the lower-case form.
    const QString playImg  = attrs.value(QStringLiteral("playbitmap"));
    const QString pauseImg = attrs.value(QStringLiteral("pausebitmap"));
    const QString stopImg  = attrs.value(QStringLiteral("stopbitmap"));
    QString img;
    if      (ctx.host->isPlaying()) img = playImg;
    else if (ctx.host->isPaused())  img = pauseImg;
    else                            img = stopImg;
    if (img.isEmpty()) return;
    // Classic-Winamp status bitmaps often have different internal
    // left padding (wa.play has 4 cols of padding, wa.pause has 0,
    // wa.stop has 2) and need to be aligned so their leftmost
    // visible column lands at the same x as the play bitmap's.
    // Modern WACUP / Winamp Modern bitmaps are *wide* — a single
    // 90×29 bitmap spans all three transport buttons and encodes
    // which one is highlighted by varying the visible column
    // (play=col 0, pause=col 30, stop=col 60).  Applying the
    // classic alignment shift to modern bitmaps mis-paints the
    // active highlight onto the wrong button (pause highlight
    // ends up over Play, etc).  Gate the shift to small offsets
    // (< 8 px) so the classic case still corrects but the modern
    // multi-button bitmap is left alone.
    const int playFvc = firstVisibleCol(*ctx.bmp, playImg);
    const int myFvc   = firstVisibleCol(*ctx.bmp, img);
    int xShift = playFvc - myFvc;
    if (qAbs(xShift) >= 8) xShift = 0;
    QHash<QString, QString> a = attrs;
    a.insert(QStringLiteral("image"), img);
    if (xShift != 0) {
        // Parse via toDouble()->truncate (Wasabi XML coords may be
        // fractional, e.g. y="26.9"); plain toInt() rejects decimals and
        // would snap x to 0.  Matches Widget::resolveRectFromAttrs and the
        // painters' coord parser — engine-wide, no skin-specific handling.
        bool ok = false;
        int x = static_cast<int>(a.value(QStringLiteral("x")).toDouble(&ok));
        if (!ok) x = 0;
        a.insert(QStringLiteral("x"), QString::number(x + xShift));
    }
    LayerPainter::paintLayer(p, *ctx.bmp, a, canvas, ctx.windowActive);
}

}  // namespace qtWasabi
