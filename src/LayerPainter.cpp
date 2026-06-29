// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <qtWasabi/LayerPainter.h>
#include <qtWasabi/BitmapRegistry.h>

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QSize>

namespace qtWasabi::LayerPainter {

namespace {
int attrInt(const QHash<QString, QString> &a,
            const QString &key, int defVal = 0) {
    auto it = a.constFind(key);
    if (it == a.constEnd()) return defVal;
    bool ok = false;
    // Parse through toDouble() then truncate: Wasabi XML coords may be
    // fractional (Big Bento's `y="26.9"`).  toInt() rejects decimal
    // strings (ok=false) and would fall back to defVal=0, snapping every
    // fractional-coord layer to the container's top/left edge — Big
    // Bento's KBPS/KHZ labels float at the display's top instead of the
    // mid band.  Mirrors Widget::resolveRectFromAttrs.  General fix.
    const double d = it.value().toDouble(&ok);
    return ok ? int(d) : defVal;
}
bool attrBool(const QHash<QString, QString> &a, const QString &key) {
    return a.value(key) == QStringLiteral("1");
}
// Parse a Wasabi "r,g,b" / "r,g,b,a" colour string into a QColor.
QColor parseColor(const QString &csv) {
    if (csv.isEmpty()) return QColor();
    const auto parts = csv.split(QChar(','));
    if (parts.size() < 3) return QColor();
    bool ok = false;
    const int r = parts[0].trimmed().toInt(&ok); if (!ok) return QColor();
    const int g = parts[1].trimmed().toInt(&ok); if (!ok) return QColor();
    const int b = parts[2].trimmed().toInt(&ok); if (!ok) return QColor();
    const int a = (parts.size() >= 4) ? parts[3].trimmed().toInt() : 255;
    return QColor(qBound(0, r, 255), qBound(0, g, 255),
                  qBound(0, b, 255), qBound(0, a, 255));
}
}  // namespace

bool paintLayer(QPainter *p, BitmapRegistry &reg,
                const QHash<QString, QString> &attrs,
                const QSize &containerSize, bool windowActive) {
    const QString image = attrs.value(QStringLiteral("image"));

    // Solid-colour fill — Wasabi's `file="$solid"` + `color="r,g,b"`
    // bitmaps and any layer carrying a `color=` with no resolvable
    // image.  The standard frame-divider bitmap is transparent plus a
    // 1px line, so the visible grey divider bar has to come from a fill
    // (Layout.cpp paints the synthetic frame divider this way).  When
    // both an image and a colour are present the image path wins below.
    const QColor fill = parseColor(attrs.value(QStringLiteral("color")));
    if (fill.isValid() && (image.isEmpty() || image.startsWith(QChar('$')))) {
        int x = attrInt(attrs, QStringLiteral("x"));
        int y = attrInt(attrs, QStringLiteral("y"));
        int w = attrInt(attrs, QStringLiteral("w"), 0);
        int h = attrInt(attrs, QStringLiteral("h"), 0);
        if (attrBool(attrs, QStringLiteral("relatx"))) x = containerSize.width()  + x;
        if (attrBool(attrs, QStringLiteral("relaty"))) y = containerSize.height() + y;
        if (attrBool(attrs, QStringLiteral("relatw"))) w = containerSize.width()  + w;
        if (attrBool(attrs, QStringLiteral("relath"))) h = containerSize.height() + h;
        y += attrInt(attrs, QStringLiteral("_shift_y"));
        if (w <= 0 || h <= 0) return true;
        qreal savedOp = -1.0;
        auto aIt = attrs.constFind(QStringLiteral("alpha"));
        if (aIt != attrs.constEnd()) {
            bool ok = false;
            const int a = aIt.value().toInt(&ok);
            if (ok && a >= 0 && a < 255) {
                savedOp = p->opacity();
                p->setOpacity(savedOp * (a / 255.0));
            }
        }
        p->fillRect(QRect(x, y, w, h), fill);
        if (savedOp >= 0.0) p->setOpacity(savedOp);
        return true;
    }

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
    // `_shift_y` — Layout.cpp sets this on MainFrame content
    // grandchildren when the skin has a <wasabi.menubar> so the
    // player chrome (and its sibling overlay widgets) clears the
    // menubar band.  paintLayer reads attrs directly, bypassing
    // Widget::resolveRectFromAttrs, so the shift has to be honoured
    // here too — otherwise <layer>/<button> children inherit y=17
    // literal and paint behind the menubar.
    y += attrInt(attrs, QStringLiteral("_shift_y"));
    if (w <= 0) w = src.width();
    if (h <= 0) h = src.height();

    // `activeAlpha` / `inactiveAlpha` — Wasabi's window-focus convention.
    // Every layer blits at `isActive() ? activealpha : inactivealpha`
    // (default 255).  Skins ship `.active`/`.inactive` pairs: the `.active`
    // layer carries `inactiveAlpha=0`, the `.inactive` layer `activeAlpha=0`.
    // So the focused window draws the active variants (inactive ones skip)
    // and an unfocused window draws the inactive variants (active ones
    // skip).  A partial value (a title's 128) dims rather than skips.
    // Keys only off the standard attrs → general for any skin's pairs.
    const int stateAlpha = windowActive
        ? attrInt(attrs, QStringLiteral("activealpha"),   255)
        : attrInt(attrs, QStringLiteral("inactivealpha"), 255);
    if (stateAlpha == 0) return true;

    // `alpha=` (0-255) — Wasabi layer-wide translucency.  Combine it with
    // the focus-state alpha and honour via QPainter::opacity around the
    // drawImage call.
    qreal savedOpacity = -1.0;
    {
        int a = 255;
        auto alphaIt = attrs.constFind(QStringLiteral("alpha"));
        if (alphaIt != attrs.constEnd()) {
            bool ok = false;
            const int v = alphaIt.value().toInt(&ok);
            if (ok) a = v;
        }
        const int combined = qBound(0, stateAlpha * a / 255, 255);
        if (combined < 255) {
            savedOpacity = p->opacity();
            p->setOpacity(savedOpacity * (combined / 255.0));
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

bool paintLayerAtNaturalSize(QPainter *p, BitmapRegistry &reg,
                              const QHash<QString, QString> &attrs,
                              const QSize &containerSize, bool windowActive) {
    // Variant of paintLayer for `<button>` widgets — Wasabi buttons
    // treat their bound image as a single sprite, not a tileable
    // chrome strip.  When the declared button rect is larger than
    // the bitmap (e.g. sysmenu at w=20 with a 15×13 bitmap), real
    // Wasabi draws the bitmap ONCE at its natural size positioned
    // at the rect's top-left.  Our chrome path (above) would tile
    // a partial second copy ("1.5 icons" sysmenu bug); short-circuit
    // here for non-chrome widgets.
    const QString image = attrs.value(QStringLiteral("image"));
    if (image.isEmpty()) return false;
    QImage src = reg.imageFor(image);
    if (src.isNull()) return false;

    // Window-focus active/inactive skip (see paintLayer): a titlebar
    // button's `.inactive` sprite carries activeAlpha=0 and its `.active`
    // sprite inactiveAlpha=0, so the focused window shows the active sprite
    // and an unfocused window the inactive one.
    const int stateAlpha = windowActive
        ? attrInt(attrs, QStringLiteral("activealpha"),   255)
        : attrInt(attrs, QStringLiteral("inactivealpha"), 255);
    if (stateAlpha == 0) return true;

    int x = attrInt(attrs, QStringLiteral("x"));
    int y = attrInt(attrs, QStringLiteral("y"));
    if (attrBool(attrs, QStringLiteral("relatx")))
        x = containerSize.width()  + x;
    if (attrBool(attrs, QStringLiteral("relaty")))
        y = containerSize.height() + y;
    y += attrInt(attrs, QStringLiteral("_shift_y"));

    qreal savedOpacity = -1.0;
    {
        int a = 255;
        auto alphaIt = attrs.constFind(QStringLiteral("alpha"));
        if (alphaIt != attrs.constEnd()) {
            bool ok = false;
            const int v = alphaIt.value().toInt(&ok);
            if (ok) a = v;
        }
        const int combined = qBound(0, stateAlpha * a / 255, 255);
        if (combined < 255) {
            savedOpacity = p->opacity();
            p->setOpacity(savedOpacity * (combined / 255.0));
        }
    }
    p->drawImage(x, y, src);
    if (savedOpacity >= 0) p->setOpacity(savedOpacity);
    return true;
}

}  // namespace qtWasabi::LayerPainter
