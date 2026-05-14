// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/LayerPainter.h>
#include <WasabiQt/BitmapRegistry.h>

#include <QImage>
#include <QPainter>
#include <QRect>
#include <QSize>

namespace WasabiQt::LayerPainter {

namespace {
int attrInt(const QHash<QString, QString> &a,
            const QString &key, int defVal = 0) {
    auto it = a.constFind(key);
    if (it == a.constEnd()) return defVal;
    bool ok = false;
    const int v = it.value().toInt(&ok);
    return ok ? v : defVal;
}
bool attrBool(const QHash<QString, QString> &a, const QString &key) {
    return a.value(key) == QStringLiteral("1");
}
}  // namespace

bool paintLayer(QPainter *p, BitmapRegistry &reg,
                const QHash<QString, QString> &attrs,
                const QSize &containerSize) {
    const QString image = attrs.value(QStringLiteral("image"));
    if (image.isEmpty()) return false;
    // For chrome layers (no sysregion or sysregion="1"), use the
    // cutout-baked bitmap so the chrome's alpha already carries any
    // sibling sysregion-cutout shape.  This keeps the drawer's
    // narrowing-strip + bottom-corner shape from clipping the player
    // chrome at the overlap — the chrome paint with alpha=0 at the
    // cut pixels simply doesn't overwrite what was painted underneath.
    //
    // For sysregion="-N" cutout layers themselves, we want the raw
    // bitmap — that's the mask we draw with CompositionMode_DestinationOut
    // when building the OS-level window region in computeWindowRegion.
    const QString sr = attrs.value(QStringLiteral("sysregion"));
    const bool isCutoutLayer = !sr.isEmpty() && sr.startsWith(QChar('-'));
    QImage src = isCutoutLayer ? reg.imageFor(image)
                               : reg.chromeImageFor(image);
    if (src.isNull()) return false;

    int x = attrInt(attrs, QStringLiteral("x"));
    int y = attrInt(attrs, QStringLiteral("y"));
    int w = attrInt(attrs, QStringLiteral("w"), 0);
    int h = attrInt(attrs, QStringLiteral("h"), 0);

    // relatx=1: x is taken relative to the right edge of the container
    // (`x="-10" relatx="1"` ⇒ container.width() - 10).  Same for y.
    if (attrBool(attrs, QStringLiteral("relatx"))) x = containerSize.width()  + x;
    if (attrBool(attrs, QStringLiteral("relaty"))) y = containerSize.height() + y;
    if (attrBool(attrs, QStringLiteral("relatw"))) w = containerSize.width()  + w;
    if (attrBool(attrs, QStringLiteral("relath"))) h = containerSize.height() + h;
    if (w <= 0) w = src.width();
    if (h <= 0) h = src.height();

    // `activeAlpha` / `inactiveAlpha` — Wasabi convention for
    // window-state-driven layer pairs.  Many widgets ship two
    // bitmaps (active vs inactive) and the engine fades between
    // them as the window gains/loses focus.  Without honouring
    // these, both layers paint at full opacity and the inactive
    // bitmap (carrying its own alpha-blended pixel pattern at
    // alpha=140) overlays on the active silver, producing visible
    // dark seams inside the streaks.  We assume the player is
    // *active* (it's our foreground app) and skip layers whose
    // activeAlpha is 0.
    {
        auto it = attrs.constFind(QStringLiteral("activealpha"));
        if (it != attrs.constEnd() && it.value().trimmed() ==
            QStringLiteral("0"))
            return true;
    }

    // `alpha=` (0-255) — Wasabi-style attribute for layer-wide
    // translucency.  Honour it via QPainter::opacity around the
    // drawImage call.
    qreal savedOpacity = -1.0;
    auto alphaIt = attrs.constFind(QStringLiteral("alpha"));
    if (alphaIt != attrs.constEnd()) {
        bool ok = false;
        const int a = alphaIt.value().toInt(&ok);
        if (ok && a >= 0 && a < 255) {
            savedOpacity = p->opacity();
            p->setOpacity(savedOpacity * (a / 255.0));
        }
    }
    // `tile="1"` — tile the bitmap in BOTH axes to fill the layer
    // rect.  Used by Wasabi list/edit backgrounds (e.g. the
    // colour-themes list's `wasabi.list.background` is a 36×36
    // navy patch that tiles across a 204×80 viewport).  Without
    // this the patch draws once at its native size and the rest
    // of the rect ends up black/transparent.
    const bool tile = attrs.value(QStringLiteral("tile")) ==
                       QStringLiteral("1");
    if (tile && src.width() > 0 && src.height() > 0) {
        for (int yo = 0; yo < h; yo += src.height()) {
            const int chunkH = qMin(src.height(), h - yo);
            for (int xo = 0; xo < w; xo += src.width()) {
                const int chunkW = qMin(src.width(), w - xo);
                p->drawImage(QRect(x + xo, y + yo, chunkW, chunkH),
                             src, QRect(0, 0, chunkW, chunkH));
            }
        }
    } else if (w > src.width() && h > src.height() &&
               src.width() > 0 && src.height() > 0) {
        // Destination exceeds source in BOTH axes: tile 2D.  This
        // is the Bento-style chrome pattern — small (e.g. 10×6)
        // repeatable patches stretched across the full window
        // background — where vertical scaling would warp the
        // texture.  Horizontal-only tiling falls through to the
        // next branch so 1-column gradient strips still vertically
        // scale (titlebar.center etc.).
        for (int yo = 0; yo < h; yo += src.height()) {
            const int chunkH = qMin(src.height(), h - yo);
            for (int xo = 0; xo < w; xo += src.width()) {
                const int chunkW = qMin(src.width(), w - xo);
                p->drawImage(QRect(x + xo, y + yo, chunkW, chunkH),
                             src, QRect(0, 0, chunkW, chunkH));
            }
        }
    } else if (w > src.width() && src.width() > 0) {
        // When the destination is wider than the source, TILE the
        // bitmap horizontally instead of stretching it.  Wasabi
        // chrome (titlebar.center, display.bg.center, st.center,
        // etc.) is authored as a 1-column-wide repeatable strip
        // — stretching it with QPainter's smooth interpolation
        // introduces subtle colour/alpha bleed at every scaled
        // pixel boundary, producing 1-px dark vertical seams
        // visible inside the silver bar.  Tiling at integer
        // offsets gives the seamless gradient the skin author
        // intended.  Vertical scaling is left intact — rows are
        // uniformly proportioned.
        int drawn = 0;
        const int srcH = qMin(h, src.height());
        while (drawn < w) {
            const int chunk = qMin(src.width(), w - drawn);
            p->drawImage(QRect(x + drawn, y, chunk, srcH),
                         src, QRect(0, 0, chunk, srcH));
            drawn += chunk;
        }
    } else {
        p->drawImage(QRect(x, y, w, h), src);
    }
    if (qEnvironmentVariableIntValue("WASABIQT_TRACE_LAYER") == 1) {
        const QPoint tl = p->transform().map(QPoint(x, y));
        fprintf(stderr, "[layer] image=%s pos=(%d,%d) size=%dx%d "
                "translatedAt=(%d,%d)\n",
                qPrintable(image), x, y, w, h, tl.x(), tl.y());
        fflush(stderr);
    }
    if (savedOpacity >= 0) p->setOpacity(savedOpacity);
    return true;
}

}  // namespace WasabiQt::LayerPainter
