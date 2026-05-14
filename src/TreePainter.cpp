// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/TreePainter.h>
#include <WasabiQt/Layout.h>
#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/FontRegistry.h>
#include <WasabiQt/ColorRegistry.h>
#include <WasabiQt/GammasetRegistry.h>
#include <WasabiQt/Host.h>
#include <WasabiQt/LayerPainter.h>
#include <WasabiQt/TextPainter.h>
#include <QDateTime>
#include <QImage>

#include <QHash>
#include <QPainter>
#include <QRect>
#include <QSet>
#include <QSize>
#include <QString>

namespace WasabiQt::TreePainter {

namespace {

using Layout::ResolvedWidget;

int attrInt(const QHash<QString, QString> &a,
            const QString &key, int defVal = 0) {
    auto it = a.constFind(key);
    if (it == a.constEnd()) return defVal;
    bool ok = false;
    const int v = it.value().toInt(&ok);
    return ok ? v : defVal;
}
bool attrBool(const QHash<QString, QString> &a, const QString &key) {
    auto it = a.constFind(key);
    if (it == a.constEnd()) return false;
    const QString &v = it.value();
    return v == QStringLiteral("1") || v.compare(QStringLiteral("true"),
                                                  Qt::CaseInsensitive) == 0;
}

// Resolve a widget's pixel rect against its parent's size.  Mirrors
// LayerPainter's logic — relatx/relaty against parent edge, relatw/h
// against parent dimension.  When w/h is unset (==0), defer to the
// caller (zero indicates "natural / full" depending on widget kind).
QRect resolveRect(const QHash<QString, QString> &a, const QSize &parent) {
    int x = attrInt(a, QStringLiteral("x"));
    int y = attrInt(a, QStringLiteral("y"));
    int w = attrInt(a, QStringLiteral("w"), 0);
    int h = attrInt(a, QStringLiteral("h"), 0);
    bool rw = attrBool(a, QStringLiteral("relatw"));
    bool rh = attrBool(a, QStringLiteral("relath"));
    // `fitparent="1"` is a Wasabi shortcut for "fill parent" — equiv
    // to w=0 relatw=1 h=0 relath=1, but explicit per-axis attrs still
    // override (Bento's tab grids use `fitparent="1" y="1" h="-2"
    // relath="1"` so only w is filled from parent).
    if (attrBool(a, QStringLiteral("fitparent"))) {
        if (!a.contains(QStringLiteral("w"))) rw = true;
        if (!a.contains(QStringLiteral("h"))) rh = true;
    }
    if (attrBool(a, QStringLiteral("relatx"))) x = parent.width()  + x;
    if (attrBool(a, QStringLiteral("relaty"))) y = parent.height() + y;
    if (rw) w = parent.width()  + w;
    if (rh) h = parent.height() + h;
    return QRect(x, y, w, h);
}

// Pull the painted-state bitmap id for buttons/togglebuttons.
// For M7 we only render the normal-state image; downImage/hoverImage
// are input-time decisions the embedder makes later.
QString staticImageId(const QHash<QString, QString> &a) {
    return a.value(QStringLiteral("image"));
}

struct PaintCtx {
    BitmapRegistry        *bmp;
    FontRegistry          *font;
    DisplayResolver        resolver;
    Host                  *host = nullptr;
    GammasetRegistry      *gammasets = nullptr;
    ColorRegistry         *colors = nullptr;
    int                    colorthemesSelected = 0;
    int                    colorthemesTopRow = 0;
    QRect                 *colorthemesBboxOut = nullptr;
    int                   *colorthemesTopRowOut = nullptr;
    int                    visMode = 1;
};

void paintRecursive(QPainter *p, const ResolvedWidget &node,
                    PaintCtx &ctx, const QSize &canvas) {
    if (node.attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;

    const QString &t = node.tag;


    // Move the colour-themes scrollbar thumb to mirror topRow.  The
    // thumb layer's image is `wasabi.scrollbar.vertical.button`;
    // we compute the proportional y inside the track and override
    // the layer's `y` before painting.
    if (t == QStringLiteral("layer") &&
        node.attrs.value(QStringLiteral("image")) ==
            QStringLiteral("wasabi.scrollbar.vertical.button") &&
        ctx.gammasets && ctx.colorthemesBboxOut) {
        QHash<QString, QString> a = node.attrs;
        const int names = ctx.gammasets->names().size();
        const int rowsVisible = 8;          // 80 px viewport / 10 px-per-row
        // Track between the top arrow (y=5..22) and bottom arrow
        // (y=68..85): thumb's valid y range is 22..68-31 = 22..37.
        // Thumb bitmap is 13×31.
        const int trackTop = 22;
        const int trackBot = 68;
        const int thumbH   = 31;
        const int travel   = qMax(0, (trackBot - trackTop) - thumbH);
        int top = ctx.colorthemesTopRow;
        const int maxTop = qMax(0, names - rowsVisible);
        if (top > maxTop) top = maxTop;
        if (top < 0)      top = 0;
        const double frac = (maxTop > 0)
                          ? double(top) / double(maxTop)
                          : 0.0;
        const int newY = trackTop + int(frac * travel);
        a.insert(QStringLiteral("y"), QString::number(newY));
        LayerPainter::paintLayer(p, *ctx.bmp, a, canvas);
        return;
    }

    // mono/stereo "lit" indicator: classic skins ship `<layer
    // id="mono" image="...mono.inactive"/>` + `<layer id="stereo"
    // image="...stereo.inactive"/>`, and a Maki script (monoster.maki)
    // swaps the active one's image to its `.active` variant based on
    // host->channelCount().  Replicate that statically here so the
    // indicator works without scripting.
    if (t == QStringLiteral("layer") && ctx.host &&
        (node.id == QStringLiteral("mono") ||
         node.id == QStringLiteral("stereo"))) {
        const int ch = ctx.host->channelCount();
        const bool active = (node.id == QStringLiteral("mono")  && ch == 1) ||
                            (node.id == QStringLiteral("stereo") && ch >= 2);
        if (active) {
            QString img = node.attrs.value(QStringLiteral("image"));
            const QString lit = img.endsWith(QStringLiteral(".inactive"))
                ? img.chopped(9) + QStringLiteral(".active")
                : img + QStringLiteral(".active");
            if (ctx.bmp->find(lit)) {
                QHash<QString, QString> a = node.attrs;
                a.insert(QStringLiteral("image"), lit);
                LayerPainter::paintLayer(p, *ctx.bmp, a, canvas);
                return;
            }
        }
    }

    if (t == QStringLiteral("layer")) {
        // Cutout-mask layers (`sysregion="-1"` / `sysregion="-2"`)
        // are pure-black/pure-alpha bitmaps used ONLY by
        // computeWindowRegion to mark pixels for region exclusion;
        // they are NOT meant to render onto the visible tree.
        // Drawing them here paints solid black into the rounded-
        // corner cutout area — the region buffer correctly clears
        // the alpha there, but the visible buffer ends up with
        // BLACK pixels under the transparent ones, so the corner
        // appears black instead of transparent.
        const QString sr = node.attrs.value(QStringLiteral("sysregion"));
        if (!sr.isEmpty() && sr.startsWith(QChar('-')))
            return;

        // Special-case the Volume fill bar: the `volumebar` layer is
        // a thin progress strip whose `w` is normally driven by
        // `volume.maki` (`volumebar.setXmlParam("w", …)`) to grow
        // with the current volume.  Until the runtime drives that,
        // fall back to deriving `w` from the host's volume slider
        // position so the user sees a live fill matching the thumb
        // instead of a fixed 10 px stub.
        //
        // Geometry (player-normal Modern):
        //   Volume slider: x=183, w=86; thumb 21 px wide; travel = 65.
        //   Volumebar layer:  x=185, so 2 px right of slider's left.
        //   Thumb's centre at x = slider.x + pos*travel + thumb.w/2
        //                      = 183 + 65*pos + 10
        //   Volumebar.w = thumb_centre - volumebar.x
        //               = (183 + 65*pos + 10) - 185
        //               = 65*pos + 8
        if (node.id == QStringLiteral("volumebar") && ctx.host) {
            const double vol = ctx.host->sliderPosition(
                QStringLiteral("VOLUME"));
            if (vol >= 0.0) {
                QHash<QString, QString> a = node.attrs;
                const int travel = 65;
                const int offset = 8;
                const int newW = qMax(1, int(vol * travel + offset));
                a.insert(QStringLiteral("w"), QString::number(newW));
                if (qEnvironmentVariableIntValue("WASABIQT_TRACE_VOL") == 1) {
                    fprintf(stderr, "[volumebar] vol=%.3f w=%d xform=(%g,%g)\n",
                            vol, newW,
                            p->transform().dx(), p->transform().dy());
                    fflush(stderr);
                }
                LayerPainter::paintLayer(p, *ctx.bmp, a, canvas);
                return;
            }
        }
        LayerPainter::paintLayer(p, *ctx.bmp, node.attrs, canvas);
        return;
    }

    // <ProgressGrid> — the seek-progress fill bar that grows from the
    // left as the song plays.  Skin XML declares it with `left=` /
    // `middle=` (3-slice bitmaps) and a host-driven width.  We tile
    // `middle` from the rect's left up to position/duration × rect.w.
    if (t == QStringLiteral("progressgrid")) {
        const QRect r = resolveRect(node.attrs, canvas);
        if (r.width() <= 0 || r.height() <= 0) return;
        double frac = 0.0;
        if (ctx.host) {
            const double pos = ctx.host->sliderPosition(
                QStringLiteral("SEEK"));
            if (pos >= 0.0) frac = qBound(0.0, pos, 1.0);
        }
        const int filled = int(r.width() * frac);
        if (filled <= 0) return;
        // Prefer `middle` — that's the repeating fill — falling back
        // to `image` if the skin author used the simpler form.
        QString midId = node.attrs.value(QStringLiteral("middle"));
        if (midId.isEmpty())
            midId = node.attrs.value(QStringLiteral("image"));
        if (midId.isEmpty()) return;
        QImage src = ctx.bmp->imageFor(midId);
        if (src.isNull()) return;
        const int srcH = qMin(r.height(), src.height());
        int drawn = 0;
        while (drawn < filled) {
            const int chunk = qMin(src.width(), filled - drawn);
            p->drawImage(QRect(r.x() + drawn, r.y(), chunk, srcH),
                         src, QRect(0, 0, chunk, srcH));
            drawn += chunk;
        }
        return;
    }

    // <images source="volume|balance" images="<bitmap-id>"
    //         imagesspacing="<stride>" w=... h=.../>
    // Multi-frame bitmap strip indexed by a host-driven value.  Used
    // by classic Winamp skins (DeClassified et al.) for the volume
    // rail and balance rail — VOLUME.BMP / BALANCE.BMP are a vertical
    // strip of 28 frames; the widget picks one based on volume (0..1)
    // or balance (-1..+1) and blits it as the rail background.
    if (t == QStringLiteral("images")) {
        const QRect r = resolveRect(node.attrs, canvas);
        if (r.width() <= 0 || r.height() <= 0) return;
        QImage src = ctx.bmp->imageFor(
            node.attrs.value(QStringLiteral("images")));
        if (src.isNull()) return;
        const int stride = node.attrs.value(
            QStringLiteral("imagesspacing")).toInt();
        if (stride <= 0) return;
        const int frames = src.height() / stride;
        if (frames <= 0) return;
        // Resolve the source value.  Wasabi's `source=` names the
        // value the widget is bound to.  We support the two classic
        // bindings; everything else falls back to the middle frame.
        double v = 0.5;
        const QString src_ = node.attrs.value(
            QStringLiteral("source")).toLower();
        if (ctx.host) {
            if (src_ == QStringLiteral("volume")) {
                double p = ctx.host->sliderPosition(
                    QStringLiteral("VOLUME"));
                if (p >= 0.0) v = qBound(0.0, p, 1.0);
            } else if (src_ == QStringLiteral("balance") ||
                       src_ == QStringLiteral("pan")) {
                double p = ctx.host->sliderPosition(
                    QStringLiteral("PAN"));
                // Balance: -1..+1 → 0..1
                if (p >= -1.0) v = qBound(0.0, (p + 1.0) * 0.5, 1.0);
            }
        }
        int frame = qBound(0, int(v * (frames - 1) + 0.5), frames - 1);
        const QRect srcRect(0, frame * stride, src.width(), r.height());
        p->drawImage(r, src, srcRect);
        return;
    }

    // <status playBitmap="X" pauseBitmap="Y" stopBitmap="Z"/> —
    // classic-skin widget that chooses one of three bitmaps based on
    // the host's playback state.  Without a Maki script driving
    // visibility, qtWasabi statically picks the matching bitmap and
    // paints it as a regular layer.
    if (t == QStringLiteral("status") && ctx.host) {
        // SkinXml lowercases attr names; look up the lower-case form.
        const QString playImg = node.attrs.value(QStringLiteral("playbitmap"));
        const QString pauseImg = node.attrs.value(QStringLiteral("pausebitmap"));
        const QString stopImg = node.attrs.value(QStringLiteral("stopbitmap"));
        QString img;
        if      (ctx.host->isPlaying()) img = playImg;
        else if (ctx.host->isPaused())  img = pauseImg;
        else                            img = stopImg;
        if (!img.isEmpty()) {
            // Classic-Winamp status bitmaps often have different
            // internal left padding (wa.play has 4 cols of padding,
            // wa.pause has 0, wa.stop has 2).  Align all three so
            // their leftmost visible column lands at the same x as
            // the play bitmap's — this is what produces the uniform
            // 3-px gap from the LED indicator strip that the upstream
            // reference shows in every playback state.
            auto firstVisibleCol = [&](const QString &id) -> int {
                if (id.isEmpty()) return 0;
                QImage im = ctx.bmp->imageFor(id);
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
            };
            const int playFvc = firstVisibleCol(playImg);
            const int myFvc = firstVisibleCol(img);
            const int xShift = playFvc - myFvc;
            QHash<QString, QString> a = node.attrs;
            a.insert(QStringLiteral("image"), img);
            if (xShift != 0) {
                bool ok = false;
                int x = a.value(QStringLiteral("x")).toInt(&ok);
                if (!ok) x = 0;
                a.insert(QStringLiteral("x"), QString::number(x + xShift));
            }
            LayerPainter::paintLayer(p, *ctx.bmp, a, canvas);
        }
        return;
    }

    if (t == QStringLiteral("button")          ||
        t == QStringLiteral("togglebutton")    ||
        t == QStringLiteral("nstatesbutton")) {
        // <NStatesButton> is a multi-state button (e.g. Repeat:
        // off/all/one).  Real Wasabi cycles through `nstates` and
        // suffixes `image` with the current state index — e.g. the
        // RepeatDisplay layer declares `image="player.songinfo.
        // repeat"` but the actual bitmap ids are `repeat0`,
        // `repeat1`, `repeat2`.  Until SkinRuntime drives the
        // current state, fall back to state 0 if the bare `image`
        // id is not registered as a bitmap.
        QHash<QString, QString> a = node.attrs;
        QString img = staticImageId(a);
        if (t == QStringLiteral("nstatesbutton") &&
            !img.isEmpty() && !ctx.bmp->find(img))
            img += QStringLiteral("0");
        a.insert(QStringLiteral("image"), img);
        LayerPainter::paintLayer(p, *ctx.bmp, a, canvas);
        return;
    }

    // <Grid> — 3-slice horizontally-stretchable bitmap chrome.
    // Used by the drawer's tab buttons (Equalizer/Options/Color
    // Themes) and other Wasabi UI elements.  `left=` and `right=`
    // are fixed-width endcaps; `middle=` tiles between them to
    // fill the gap.
    if (t == QStringLiteral("grid")) {
        const QRect r = resolveRect(node.attrs, canvas);
        if (r.width() <= 0 || r.height() <= 0) return;
        QImage leftPm = ctx.bmp->imageFor(
            node.attrs.value(QStringLiteral("left")));
        QImage middlePm = ctx.bmp->imageFor(
            node.attrs.value(QStringLiteral("middle")));
        QImage rightPm = ctx.bmp->imageFor(
            node.attrs.value(QStringLiteral("right")));
        const int leftW = leftPm.isNull() ? 0 : leftPm.width();
        const int rightW = rightPm.isNull() ? 0 : rightPm.width();
        const int rh = qMin(r.height(),
                            !middlePm.isNull() ? middlePm.height()
                            : !leftPm.isNull() ? leftPm.height()
                            : r.height());
        if (!leftPm.isNull())
            p->drawImage(QRect(r.x(), r.y(), qMin(leftW, r.width()), rh),
                         leftPm,
                         QRect(0, 0, qMin(leftW, r.width()), rh));
        if (!rightPm.isNull() && r.width() > leftW)
            p->drawImage(QRect(r.x() + r.width() - rightW, r.y(),
                                qMin(rightW, r.width()), rh),
                         rightPm,
                         QRect(0, 0, qMin(rightW, r.width()), rh));
        if (!middlePm.isNull() && r.width() > leftW + rightW) {
            int mx = r.x() + leftW;
            const int mEnd = r.x() + r.width() - rightW;
            const int mw = middlePm.width();
            while (mx < mEnd) {
                const int chunk = qMin(mw, mEnd - mx);
                p->drawImage(QRect(mx, r.y(), chunk, rh),
                             middlePm,
                             QRect(0, 0, chunk, rh));
                mx += chunk;
            }
        }
        return;
    }

    if (t == QStringLiteral("text") || t == QStringLiteral("songticker")) {
        // The displayed string comes through the resolver as
        // "songtitle" / "songinfo" or whatever the embedder wires up.
        QHash<QString, QString> a = node.attrs;
        if (t == QStringLiteral("songticker") &&
            a.value(QStringLiteral("display")).isEmpty()) {
            a.insert(QStringLiteral("display"),
                     QStringLiteral("songtitle"));
        }
        // Ticker scrolling: <songticker> is implicitly a ticker, and
        // <text ticker="1"> opts into scrolling explicitly.  The text
        // scrolls leftward when the resolved string is wider than the
        // widget rect; wraps with a small gap; loops continuously.
        const bool isTicker =
            (t == QStringLiteral("songticker")) ||
            (a.value(QStringLiteral("ticker")) == QStringLiteral("1"));
        if (isTicker && ctx.font && ctx.bmp) {
            // Resolve display string the same way TextPainter does
            // so we can measure its bitmap-font width.
            const QString fontId = a.value(QStringLiteral("font"));
            const auto *fd = ctx.font->find(fontId);
            QString tickText;
            const QString display = a.value(QStringLiteral("display"));
            if (ctx.resolver && !display.isEmpty())
                tickText = ctx.resolver(display);
            // Mirror paintText's id-based fallback so the ticker
            // pre-check sees the same string the painter will draw.
            if (tickText.isEmpty() && ctx.resolver) {
                const QString id = a.value(QStringLiteral("id"));
                if (!id.isEmpty()) tickText = ctx.resolver(id);
            }
            if (tickText.isEmpty())
                tickText = a.value(QStringLiteral("default"));
            if (tickText.isEmpty())
                tickText = a.value(QStringLiteral("text"));
            const QRect r = resolveRect(a, canvas);
            if (fd && fd->charWidth > 0 && r.width() > 0 &&
                !tickText.isEmpty()) {
                const int tickW =
                    tickText.size() * fd->charWidth +
                    qMax(0, tickText.size() - 1) * fd->hSpacing;
                if (tickW > r.width()) {
                    // Scroll: 30 px/s, with a `gap` of charWidth*4
                    // before the text loops back into view.
                    const int gap = fd->charWidth * 4;
                    const int totalW = tickW + gap;
                    const qint64 ms =
                        QDateTime::currentMSecsSinceEpoch();
                    // Speed comes from the skin: in real Wasabi a Maki
                    // script calls Songticker.setSpeed(N) at startup.
                    // Until SkinRuntime drives that universally, the
                    // skin can set it on the XML via `tickspeed=` or
                    // `speed=`, and the renderer also honours Modern
                    // PP's `pixelsperframe` (Wasabi's actual attr name
                    // for songticker scroll rate).  Default = 12 px/s,
                    // matching the Wasabi Modern stock speed.
                    auto attrInt = [&](const QString &k) {
                        return a.value(k).toInt();
                    };
                    int speed = attrInt(QStringLiteral("tickspeed"));
                    if (speed <= 0) speed = attrInt(QStringLiteral("speed"));
                    if (speed <= 0)
                        speed = attrInt(QStringLiteral("pixelsperframe"))
                                * 30;  // ~30 fps in classic Wasabi
                    if (speed <= 0) speed = 12;
                    const int offset =
                        int((ms * speed / 1000) % qint64(totalW));
                    p->save();
                    p->setClipRect(r);
                    p->translate(-offset, 0);
                    TextPainter::paintText(p, *ctx.font, *ctx.bmp, a,
                                           canvas, ctx.resolver,
                                           ctx.colors, ctx.gammasets,
                                           /*clipToWidget=*/false);
                    p->translate(totalW, 0);
                    TextPainter::paintText(p, *ctx.font, *ctx.bmp, a,
                                           canvas, ctx.resolver,
                                           ctx.colors, ctx.gammasets,
                                           /*clipToWidget=*/false);
                    p->restore();
                    return;
                }
            }
        }
        TextPainter::paintText(p, *ctx.font, *ctx.bmp, a, canvas,
                               ctx.resolver, ctx.colors, ctx.gammasets);
        return;
    }

    // <ColorThemes:List> — the drawer's colour-themes picker.  XML
    // tag normalises to `colorthemes_list`.  Render the list of
    // available gammasets one row per line, highlight the active
    // (or visually-selected) one, and remember the rect so the
    // embedder's click handler can map a Y-coord to a row index.
    if (t == QStringLiteral("colorthemes_list") && ctx.gammasets) {
        const QRect r = resolveRect(node.attrs, canvas);
        if (r.width() <= 0 || r.height() <= 0) return;
        // Reserve the right ~14 px for the scrollbar chrome
        // (wasabi.scrollbar.vertical.*) sitting on top of the
        // list's right edge.  Otherwise the highlight bar would
        // bleed into the scroll thumb.
        const int scrollbarW = 14;
        const QRect listRect(r.x(), r.y(),
                              qMax(0, r.width() - scrollbarW),
                              r.height());
        if (ctx.colorthemesBboxOut) {
            const QPoint topLeft = p->transform().map(listRect.topLeft());
            *ctx.colorthemesBboxOut = QRect(topLeft, listRect.size());
        }
        QStringList names = ctx.gammasets->names();
        std::sort(names.begin(), names.end(),
                  [](const QString &a, const QString &b){
                      return a.compare(b, Qt::CaseInsensitive) < 0;
                  });
        const int rowH = 10;
        int sel = ctx.colorthemesSelected;
        if (sel < 0 || sel >= names.size()) {
            sel = 0;
            const auto *act = ctx.gammasets->active();
            if (act) {
                for (int i = 0; i < names.size(); ++i)
                    if (names[i] == act->name) { sel = i; break; }
            }
        }
        const int maxRows = qMax(0, (listRect.height() - 4) / rowH);
        // `topRow` is the index of the first visible name.  Clamp
        // to the valid range, but DON'T auto-pull the selection
        // into view — doing that would prevent the user from
        // scrolling past their last-clicked row, since every paint
        // would snap topRow back to align with `sel`.  The embedder
        // can choose to auto-scroll on selection-change separately.
        int topRow = ctx.colorthemesTopRow;
        const int maxTop = qMax(0, names.size() - maxRows);
        if (topRow < 0) topRow = 0;
        if (topRow > maxTop) topRow = maxTop;
        if (ctx.colorthemesTopRowOut)
            *ctx.colorthemesTopRowOut = topRow;
        QFont qf(QStringLiteral("sans-serif"));
        qf.setPixelSize(8);
        p->save();
        p->setFont(qf);
        const int yStart = listRect.y() + 2;
        for (int i = 0; i < maxRows && (topRow + i) < names.size(); ++i) {
            const int nameIdx = topRow + i;
            const QRect row(listRect.x() + 2, yStart + i * rowH,
                            listRect.width() - 4, rowH);
            if (nameIdx == sel) {
                p->fillRect(row, QColor(80, 110, 175, 200));
                p->setPen(QColor(255, 255, 255));
            } else {
                p->setPen(QColor(220, 225, 235));
            }
            p->drawText(row.adjusted(3, 0, -3, 0),
                        Qt::AlignVCenter | Qt::AlignLeft,
                        names[nameIdx]);
        }
        p->restore();
        return;
    }

    if (t == QStringLiteral("vis")) {
        // Spectrum-style bars.  Real FFT-driven spectrum is M11+
        // work; for now bar heights are pseudo-random per band but
        // multiplied by Host::audioLevel() (recent RMS) so the
        // whole visualisation bounces with the audio.  No host =
        // 0-amplitude bars (chrome stays visible but doesn't move).
        const QRect r = resolveRect(node.attrs, canvas);
        if (r.width() > 0 && r.height() > 0) {
            // Visualisation bar colour: the `<vis>` widget itself
            // carries a `gammagroup="DisplayVis"` attribute — the
            // colorband colours are LITERAL r,g,b values that
            // pass through that group under the active gammaset.
            // (This is why Good Ol' Winamp turns the spectrum
            // green: DisplayVis transform is `-4096,0,-4096`,
            // which zeroes R and B but keeps G.)
            const QString c1 = node.attrs.value(QStringLiteral("colorband1"));
            QColor band(255, 255, 255);
            if (c1.contains(QChar(','))) {
                const auto parts = c1.split(QChar(','));
                if (parts.size() == 3)
                    band = QColor(parts[0].toInt(), parts[1].toInt(),
                                  parts[2].toInt());
            } else if (ctx.colors) {
                band = ctx.colors->resolve(c1, ctx.gammasets, band);
            }
            const QString gg = node.attrs.value(
                QStringLiteral("gammagroup"));
            if (!gg.isEmpty() && ctx.gammasets) {
                const GammaGroup t = ctx.gammasets->transformFor(gg);
                // Same per-channel transform GammasetRegistry uses
                // for bitmaps, applied to the literal band colour.
                int R = band.red(), G = band.green(), B = band.blue();
                if (t.gray == 1) {
                    const int m = qMax(R, qMax(G, B));
                    R = G = B = m;
                } else if (t.gray == 2) {
                    R = G = B = (R + G + B) / 3;
                }
                if (t.boost) {
                    R = qMin(255, (R >> 1) + 127);
                    G = qMin(255, (G >> 1) + 127);
                    B = qMin(255, (B >> 1) + 127);
                }
                const int rm = 65535 + (t.r << 4);
                const int gm = 65535 + (t.g << 4);
                const int bm = 65535 + (t.b << 4);
                R = qBound(0, (R * rm) >> 16, 255);
                G = qBound(0, (G * gm) >> 16, 255);
                B = qBound(0, (B * bm) >> 16, 255);
                band = QColor(R, G, B);
            }
            const double level = ctx.host
                ? qBound(0.0, ctx.host->audioLevel() * 4.0, 1.0)
                : 0.0;
            switch (ctx.visMode) {
            case 0:  // Off
                break;
            case 1: {  // Spectrum analyzer
                const int barCount = 16;
                const int barW = r.width() / barCount;
                const int maxH = r.height() - 4;
                for (int i = 0; i < barCount; ++i) {
                    const int rawH = 4 + ((i * 17 + 3) % maxH);
                    const int h = qMax(1, int(rawH * level));
                    p->fillRect(r.x() + i * barW + 1,
                                r.y() + (r.height() - h),
                                barW - 1, h, band);
                }
                break;
            }
            case 2: {  // Oscilloscope — pseudo-waveform line
                p->save();
                QPen pen(band); pen.setWidth(1);
                p->setPen(pen);
                const int samples = r.width();
                const int mid = r.y() + r.height() / 2;
                const double amp = (r.height() / 2.0 - 2.0) * level;
                QPoint prev(r.x(), mid);
                for (int x = 1; x < samples; ++x) {
                    const double phase = x * 0.35;
                    const int y = mid +
                        int(std::sin(phase) * amp *
                            (0.5 + 0.5 * std::sin(x * 0.07)));
                    const QPoint cur(r.x() + x, y);
                    p->drawLine(prev, cur);
                    prev = cur;
                }
                p->restore();
                break;
            }
            case 3: {  // VU meter — two horizontal bars (L/R)
                const int half = r.height() / 2;
                const int filledW = int(r.width() * level);
                p->fillRect(r.x(), r.y() + 1,
                            filledW, half - 2, band);
                p->fillRect(r.x(), r.y() + half + 1,
                            filledW, half - 2, band);
                break;
            }
            }
        }
        return;
    }

    // Containers paint their children with a translated origin.
    // XUI-mangled tags (`<Wasabi:Foo>` parsed as `wasabi_foo`) act
    // as groups too — they are the inlined form of a `<groupdef
    // xuitag="Wasabi:Foo">` reference and carry the same x/y/w/h
    // as a `<group>`.  Without recognising them here their children
    // would render in their *parent's* coord space, dropping the
    // frame's x/y offset (a 10 px shift on the titlebar's contents,
    // for example).
    if (t == QStringLiteral("group")          ||
        t == QStringLiteral("container")      ||
        t == QStringLiteral("layout")         ||
        t == QStringLiteral("groupdef")       ||
        t == QStringLiteral("componentbucket")||
        t == QStringLiteral("groupxfade")     ||
        t.startsWith(QStringLiteral("wasabi_"))) {
        const QRect r = resolveRect(node.attrs, canvas);
        // A container is *collapsed* when it declares a size attr
        // (w/h, possibly with relatw/relath) that resolves to <= 0.
        // Example: AVSGroup at the end of the video/vis drawer's
        // close tween — relath=1 h=-280 with m_nativeSize.h=280 ⇒ h=0.
        // Painting its children with the parent's canvas would inflate
        // fitparent fills (video.group's black-rect background) back
        // to the full layout area — the "drawer suddenly reappears
        // behind the player" glitch right before the drawer closes.
        //
        // Containers that DON'T declare a size attr (most groupdefs
        // — they inherit from the canvas) continue to paint children
        // with the parent's canvas as before.  <layout> is the root
        // and never declares its own size, so it's exempt.
        const bool hasH = t != QStringLiteral("layout") &&
            (node.attrs.contains(QStringLiteral("h")) ||
             node.attrs.contains(QStringLiteral("relath")));
        const bool hasW = t != QStringLiteral("layout") &&
            (node.attrs.contains(QStringLiteral("w")) ||
             node.attrs.contains(QStringLiteral("relatw")));
        if ((hasH && r.height() <= 0) ||
            (hasW && r.width()  <= 0))
            return;
        QSize childSize = canvas;
        if (r.width()  > 0) childSize.setWidth (r.width());
        if (r.height() > 0) childSize.setHeight(r.height());
        // Clip children to the container's rect when it declares an
        // explicit size — prevents child bitmaps drawn at natural
        // height (e.g. AVSGroup's top-edge chrome at AVSGroup.h=1
        // would otherwise paint a full 18 px tall bitmap straight up
        // into the titlebar area mid-tween).  Containers without an
        // explicit size aren't clipped (they cover their canvas).
        const bool clipToContainer = (hasH && r.height() > 0) ||
                                     (hasW && r.width()  > 0);
        const bool translate = (r.x() != 0 || r.y() != 0)
                               && t != QStringLiteral("layout");
        // componentbucket scrolls its entries via a `_scroll` attr
        // (mutated by cb_prevpage / cb_nextpage button clicks).  Each
        // entry was materialised at y = i * entry_step at expansion
        // time; shifting the whole bucket's translate by -scroll*step
        // moves the visible window of entries.  Clipping (above) hides
        // entries that scroll off either edge.
        int scrollY = 0, scrollX = 0;
        if (t == QStringLiteral("componentbucket")) {
            const int scroll =
                node.attrs.value(QStringLiteral("_scroll")).toInt();
            const int step =
                node.attrs.value(QStringLiteral("_entry_step")).toInt();
            const bool vertical =
                node.attrs.value(QStringLiteral("vertical")) ==
                QStringLiteral("1");
            if (scroll > 0 && step > 0) {
                if (vertical) scrollY = scroll * step;
                else          scrollX = scroll * step;
            }
        }
        if (translate || clipToContainer) p->save();
        if (translate) p->translate(r.x(), r.y());
        if (clipToContainer) {
            // After translate, the local-coord clip rect starts at (0,0).
            p->setClipRect(QRect(0, 0,
                hasW ? r.width()  : canvas.width(),
                hasH ? r.height() : canvas.height()),
                Qt::IntersectClip);
        }
        if (scrollX || scrollY) p->translate(-scrollX, -scrollY);
        for (const auto &child : node.children)
            if (child) paintRecursive(p, *child, ctx, childSize);
        if (translate || clipToContainer) p->restore();
        return;
    }

    if (t == QStringLiteral("slider")) {
        const QRect r = resolveRect(node.attrs, canvas);
        if (r.width() <= 0 || r.height() <= 0) return;
        // Optional background bitmap (most Modern-skin sliders
        // paint their groove via separate <layer> siblings; some
        // do carry an `image=` attr though).
        const QString bgId = node.attrs.value(QStringLiteral("image"));
        if (!bgId.isEmpty()) {
            QHash<QString, QString> bg = node.attrs;
            bg.remove(QStringLiteral("thumb"));
            LayerPainter::paintLayer(p, *ctx.bmp, bg, canvas);
        }
        const QString thumbId = node.attrs.value(QStringLiteral("thumb"));
        if (thumbId.isEmpty()) return;
        const QString action = node.attrs.value(QStringLiteral("action"));
        // Default to a sensible resting position for actions the
        // host doesn't recognise (most commonly EQ_BAND with a
        // per-band `param=`, which neither the default Host nor
        // QtampHost expose yet).  Centred = no boost / no cut for
        // EQ; most other unknown sliders look reasonable centred
        // too.  Without this the thumb never paints and the EQ
        // sliders look like inert grooves.
        double pos = 0.5;
        if (ctx.host) {
            const double live = ctx.host->sliderPosition(action);
            if (live >= 0.0) pos = live;
        }
        QImage thumb = ctx.bmp->imageFor(thumbId);
        if (thumb.isNull()) return;
        const bool vertical = node.attrs.value(
            QStringLiteral("orientation")).compare(
            QStringLiteral("vertical"), Qt::CaseInsensitive) == 0;
        int thumbX, thumbY;
        if (vertical) {
            // Vertical sliders run top-to-bottom: pos=0 at top,
            // pos=1 at bottom.  EQ_BAND convention is pos=0.5 = 0
            // dB at the centre.
            const int travel = qMax(0, r.height() - thumb.height());
            thumbX = r.x() + (r.width() - thumb.width()) / 2;
            thumbY = r.y() + int(pos * travel);
        } else {
            const int travel = qMax(0, r.width() - thumb.width());
            thumbX = r.x() + int(pos * travel);
            thumbY = r.y() + (r.height() - thumb.height()) / 2;
        }
        p->drawImage(thumbX, thumbY, thumb);
        return;
    }

    // <animatedlayer> — Wasabi's sprite-strip animated layer.  Until
    // we wire a frame-pump timer, render the static `image=` (first
    // frame) the same way a plain <layer> would.  Most chrome usages
    // of AnimatedLayer in Modern PP use it as a static layer with a
    // few-frame "breathing" effect that's invisible when missing.
    if (t == QStringLiteral("animatedlayer")) {
        const QRect r = resolveRect(node.attrs, canvas);
        if (r.width() > 0 || r.height() > 0 ||
            !node.attrs.value(QStringLiteral("image")).isEmpty()) {
            LayerPainter::paintLayer(p, *ctx.bmp, node.attrs, canvas);
        }
        for (const auto &child : node.children)
            if (child) paintRecursive(p, *child, ctx, canvas);
        return;
    }

    // <albumart> — render the embedder-supplied album cover, scaled
    // to the widget's bbox.  Falls back to a single-colour placeholder
    // tile when no Host or no track is loaded.  Skin scripts that
    // want a default cover can <layer image="..."/> behind the
    // <AlbumArt> as a sibling, which still paints normally.
    if (t == QStringLiteral("albumart")) {
        const QRect r = resolveRect(node.attrs, canvas);
        int w = r.width(), h = r.height();
        if (w <= 0 || h <= 0) {
            // <AlbumArt> commonly sizes itself off the parent canvas
            // via relat*/no-explicit-size; fall through silently when
            // we can't determine bounds.
            for (const auto &child : node.children)
                if (child) paintRecursive(p, *child, ctx, canvas);
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
        for (const auto &child : node.children)
            if (child) paintRecursive(p, *child, ctx, canvas);
        return;
    }

    // Debug: log every tag we don't handle so Phase 7 inventory is
    // mechanical.  Suppress containers (they're handled below) and
    // the leaf nodes already covered above.
    if (::getenv("WASABIQT_TRACE_UNKNOWN_TAGS")) {
        static QSet<QString> seen;
        if (!seen.contains(t)) {
            seen.insert(t);
            ::fprintf(stderr, "[paint] unhandled tag: %s (id=%s)\n",
                      t.toLocal8Bit().constData(),
                      node.id.toLocal8Bit().constData());
        }
    }
    // <rect filled="1" color="R,G,B" /> — solid colored rectangle.
    // Wasabi uses it for the video.group black-fill background and
    // tooltip-border rects.  `filled="0"` draws an outline only.
    if (t == QStringLiteral("rect")) {
        const QRect r = resolveRect(node.attrs, canvas);
        if (r.width() <= 0 || r.height() <= 0) return;
        QColor c(0, 0, 0);
        const QString col = node.attrs.value(QStringLiteral("color"));
        if (col.contains(QChar(','))) {
            const auto parts = col.split(QChar(','));
            if (parts.size() == 3)
                c = QColor(parts[0].toInt(), parts[1].toInt(),
                           parts[2].toInt());
        } else if (ctx.colors) {
            c = ctx.colors->resolve(col, ctx.gammasets, c);
        }
        if (node.attrs.value(QStringLiteral("filled")) ==
            QStringLiteral("0"))
            p->setPen(c), p->drawRect(r);
        else
            p->fillRect(r, c);
        return;
    }
    // <windowholder> / <wmh> — host slot for an embedded sub-window
    // (video output, AVS frame, …).  We don't have the embedded
    // surface in our renderer, so paint a black placeholder rect
    // that visually matches what real Winamp shows when the embedded
    // component is loading / unavailable.
    if (t == QStringLiteral("windowholder") ||
        t == QStringLiteral("wmh")) {
        const QRect r = resolveRect(node.attrs, canvas);
        if (r.width() > 0 && r.height() > 0)
            p->fillRect(r, QColor(0, 0, 0));
        return;
    }
    // <edit> / <wasabi.edit.box> — minimal placeholder: paint the
    // declared default / current text inside a 1-px outlined rect at
    // the widget's bounds.  Real text editing requires a focus +
    // input-method handoff — that's a milestone of its own.
    if (t == QStringLiteral("edit") ||
        t == QStringLiteral("wasabi.edit.box")) {
        const QRect r = resolveRect(node.attrs, canvas);
        if (r.width() > 0 && r.height() > 0) {
            p->fillRect(r, QColor(20, 30, 60));
            p->setPen(QColor(120, 130, 160));
            p->drawRect(r.adjusted(0, 0, -1, -1));
            QString s = node.attrs.value(QStringLiteral("text"));
            if (s.isEmpty()) s = node.attrs.value(QStringLiteral("default"));
            if (!s.isEmpty()) {
                QFont qf(QStringLiteral("sans-serif"));
                qf.setPixelSize(qMax(8, r.height() - 4));
                p->save();
                p->setFont(qf);
                p->setPen(QColor(220, 225, 235));
                p->drawText(r.adjusted(4, 0, -4, 0),
                            Qt::AlignVCenter | Qt::AlignLeft, s);
                p->restore();
            }
        }
        return;
    }
    // <vis>, <guilist>, <treelist>, <scrollbar>, <popup>, ...
    // — painted in later milestones.  Recurse so any children that we
    // DO know how to render still get reached.
    for (const auto &child : node.children)
        if (child) paintRecursive(p, *child, ctx, canvas);
}

}  // namespace

void paintTree(QPainter *p, const ResolvedWidget &root,
               BitmapRegistry &reg, FontRegistry &fontReg,
               const QSize &canvas, const DisplayResolver &resolver) {
    PaintCtx ctx{&reg, &fontReg, resolver, nullptr};
    paintRecursive(p, root, ctx, canvas);
}

void paintTree(QPainter *p, const ResolvedWidget &root,
               BitmapRegistry &reg, FontRegistry &fontReg,
               const QSize &canvas, Host *host) {
    PaintCtx ctx{&reg, &fontReg, makeDefaultDisplayResolver(host), host};
    paintRecursive(p, root, ctx, canvas);
}

void paintTree(QPainter *p, const ResolvedWidget &root,
               BitmapRegistry &reg, FontRegistry &fontReg,
               const QSize &canvas, Host *host,
               GammasetRegistry *gammasets,
               ColorRegistry *colors,
               int colorthemesSelectedRow,
               int colorthemesTopRowIn,
               QRect *colorthemesListBboxOut,
               int  *colorthemesTopRowOut,
               int  visMode) {
    PaintCtx ctx{&reg, &fontReg, makeDefaultDisplayResolver(host), host,
                 gammasets, colors, colorthemesSelectedRow,
                 colorthemesTopRowIn,
                 colorthemesListBboxOut, colorthemesTopRowOut,
                 visMode};
    paintRecursive(p, root, ctx, canvas);
}

}  // namespace WasabiQt::TreePainter
