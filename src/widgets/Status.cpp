// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Status.h"

#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/Host.h>
#include <WasabiQt/LayerPainter.h>
#include <WasabiQt/PaintCtx.h>

#include <QImage>
#include <QPainter>

namespace WasabiQt {

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
    // wa.stop has 2).  Align all three so their leftmost visible
    // column lands at the same x as the play bitmap's.
    const int playFvc = firstVisibleCol(*ctx.bmp, playImg);
    const int myFvc   = firstVisibleCol(*ctx.bmp, img);
    const int xShift  = playFvc - myFvc;
    QHash<QString, QString> a = attrs;
    a.insert(QStringLiteral("image"), img);
    if (xShift != 0) {
        bool ok = false;
        int x = a.value(QStringLiteral("x")).toInt(&ok);
        if (!ok) x = 0;
        a.insert(QStringLiteral("x"), QString::number(x + xShift));
    }
    LayerPainter::paintLayer(p, *ctx.bmp, a, canvas);
}

}  // namespace WasabiQt
