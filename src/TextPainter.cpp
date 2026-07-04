// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <qtWasabi/TextPainter.h>
#include <qtWasabi/FontRegistry.h>
#include <qtWasabi/BitmapRegistry.h>
#include <qtWasabi/ColorRegistry.h>
#include <qtWasabi/GammasetRegistry.h>

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QSize>
#include <QString>

namespace qtWasabi {

int wasabiFontPixelSize(int lfHeight, const QString &family,
                        bool bold, bool italic) {
    if (lfHeight <= 0) return 1;
    static QHash<quint64, int> cache;
    const quint64 key =
        (quint64(uint(qHash(family))) << 20) ^
        (quint64(uint(lfHeight)) << 2) ^ (bold ? 2u : 0u) ^ (italic ? 1u : 0u);
    if (auto it = cache.constFind(key); it != cache.constEnd()) return *it;
    QFont f;
    if (!family.isEmpty()) f.setFamily(family);
    f.setBold(bold);
    f.setItalic(italic);
    // Binary-search the largest pixelSize whose QFontMetrics::height()
    // (ascent+descent) does not exceed the Win32 lfHeight cell — that's the
    // pixel size CreateFontW(lfHeight) maps to.  WASABIQT_FONT_RATIO still
    // overrides for ad-hoc tuning (N,D → lfHeight*N/D).
    int px;
    if (const char *r = ::getenv("WASABIQT_FONT_RATIO")) {
        int a = 0, b = 0;
        if (sscanf(r, "%d,%d", &a, &b) == 2 && a > 0 && b > 0)
            px = qMax(1, (lfHeight * a + b / 2) / b);
        else px = qMax(1, lfHeight);
    } else {
        int lo = 1, hi = lfHeight * 2, best = 1;
        while (lo <= hi) {
            const int mid = (lo + hi) / 2;
            f.setPixelSize(mid);
            if (QFontMetrics(f).height() <= lfHeight) { best = mid; lo = mid + 1; }
            else hi = mid - 1;
        }
        px = best;
    }
    cache.insert(key, px);
    return px;
}

}  // namespace qtWasabi

namespace qtWasabi::TextPainter {

namespace {

int attrInt(const QHash<QString, QString> &a,
            const QString &key, int defVal = 0) {
    auto it = a.constFind(key);
    if (it == a.constEnd()) return defVal;
    bool ok = false;
    // Parse through toDouble() then truncate: Wasabi XML coords may be
    // fractional (Big Bento's `y="26.9"`).  toInt() rejects decimal
    // strings (ok=false) and would fall back to defVal=0, snapping every
    // fractional-coord widget to the container's top/left edge.  Mirrors
    // Widget::resolveRectFromAttrs.  General: any skin, any widget.
    const double d = it.value().toDouble(&ok);
    return ok ? int(d) : defVal;
}
bool attrBool(const QHash<QString, QString> &a, const QString &key) {
    auto it = a.constFind(key);
    if (it == a.constEnd()) return false;
    const QString &v = it.value();
    return v == QStringLiteral("1") ||
           v.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
}

}  // namespace

bool paintText(QPainter *p,
               FontRegistry &fontReg, BitmapRegistry &bmpReg,
               const QHash<QString, QString> &attrs,
               const QSize &containerSize,
               const DisplayResolver &resolver,
               const ColorRegistry *colors,
               const GammasetRegistry *gammasets,
               bool clipToWidget) {
    // Resolve font.  Wasabi text widgets fall back to a system
    // default font when the widget's `font=` attr is empty (canonical
    // case: Bento's `<SongTicker>` declares no font — the player
    // widget uses a system Arial-equivalent).  Substitute
    // "sans-serif" here so TextPainter can still render the track
    // title via Qt's QFontDatabase default.
    QString fontId = attrs.value(QStringLiteral("font"));
    if (fontId.isEmpty()) fontId = QStringLiteral("sans-serif");

    const auto *fontDef = fontReg.find(fontId);
    const bool isBitmap = (fontDef != nullptr);
    if (!isBitmap) {
        // TrueType fall-through — handled at the end of this function.
    }

    // Resolve x/y/w/h with relat* against the container.
    int x = attrInt(attrs, QStringLiteral("x"));
    int y = attrInt(attrs, QStringLiteral("y"));
    int w = attrInt(attrs, QStringLiteral("w"), 0);
    int h = attrInt(attrs, QStringLiteral("h"), 0);
    if (attrBool(attrs, QStringLiteral("relatx"))) x = containerSize.width()  + x;
    if (attrBool(attrs, QStringLiteral("relaty"))) y = containerSize.height() + y;
    if (attrBool(attrs, QStringLiteral("relatw"))) w = containerSize.width()  + w;
    if (attrBool(attrs, QStringLiteral("relath"))) h = containerSize.height() + h;
    if (w <= 0) w = isBitmap ? (fontDef->charWidth * 8) : 64;
    if (h <= 0) h = isBitmap ? fontDef->charHeight     : 16;

    // Pick the string: resolver(display=) → resolver(id=) →
    // default/text → empty.  Many Wasabi skins (Winamp Modern PP,
    // Bento, etc.) declare text widgets with `display=""` and rely
    // on a Maki script doing `Bitrate.setXmlParam("text", "320")`
    // to populate it at runtime.  Until SkinRuntime drives that,
    // fall back to using the widget's `id` as a display key — every
    // Modern skin follows the same naming convention (Bitrate,
    // Frequency, Songticker, Time, …), so the same Host resolver
    // that handles `display="songbitrate"` also handles `id="Bitrate"`.
    QString text;
    const QString display = attrs.value(QStringLiteral("display"));
    if (resolver && !display.isEmpty()) text = resolver(display);
    if (text.isEmpty() && resolver) {
        const QString id = attrs.value(QStringLiteral("id"));
        if (!id.isEmpty()) text = resolver(id);
    }
    if (text.isEmpty()) text = attrs.value(QStringLiteral("default"));
    if (text.isEmpty()) text = attrs.value(QStringLiteral("text"));
    if (qEnvironmentVariableIntValue("WASABIQT_TRACE_META") == 1) {
        std::fprintf(stderr,
              "[textpaint] id='%s' resolved='%s' attrtext='%s' display='%s' rect=%dx%d@(%d,%d)\n",
              attrs.value(QStringLiteral("id")).toLocal8Bit().constData(),
              text.toLocal8Bit().constData(),
              attrs.value(QStringLiteral("text")).toLocal8Bit().constData(),
              display.toLocal8Bit().constData(), w, h, x, y);
    }
    if (text.isEmpty()) return true;

    if (attrBool(attrs, QStringLiteral("forceuppercase")))
        text = text.toUpper();

    const QString align = attrs.value(QStringLiteral("align")).toLower();

    // Wasabi text widgets clip their content to the declared rect —
    // long song titles in songticker scroll/clip inside `w` instead
    // of leaking into the kbps/kHz area, time strings can never
    // overflow the LCD frame, etc.  Callers that pre-translate the
    // painter (songticker scroll path) own their own clip in device
    // coords and pass clipToWidget=false to skip ours — re-clipping
    // through a translated transform mis-intersects.
    p->save();
    if (clipToWidget)
        p->setClipRect(QRect(x, y, w, h), Qt::IntersectClip);

    if (isBitmap) {
        // ── bitmap font path ───────────────────────────────────
        // `timecolonwidth` lets a skin author render the colon
        // narrower than digits — Winamp's classic BIGNUM has a slim
        // colon glyph (6 px) inside a 9-px cell, and the skin advances
        // the cursor by 6 instead of 9 to remove the dead space.
        // Wasabi's BIGNUM time-display convention:
        //   * digit cells advance by `charWidth + hSpacing`
        //   * the colon's cell is just `timecolonwidth` wide with NO
        //     trailing hSpacing — the colon's own width absorbs the
        //     visual gap to the next digit
        //   * the seconds digit reserves a small fixed right margin
        //     so it doesn't hug the LCD frame
        const int colDotW = attrInt(attrs,
            QStringLiteral("timecolonwidth"), fontDef->charWidth);
        auto cellWidth = [&](QChar c) {
            return c == u':' ? colDotW : fontDef->charWidth;
        };
        auto trailingSpace = [&](QChar c) {
            // No trailing space after the colon: its 6-wide cell IS
            // the gap to the next digit.
            return c == u':' ? 0 : fontDef->hSpacing;
        };
        int lineW = 0;
        for (int i = 0; i < text.size(); ++i) {
            lineW += cellWidth(text.at(i));
            if (i < text.size() - 1)
                lineW += trailingSpace(text.at(i));
        }
        // Wasabi's text widget reserves `hSpacing + 1` of right
        // padding for right-aligned bitmap-font text — it's the
        // "trailing hSpacing after the last char" that Wasabi adds
        // before measuring against the widget's right edge.  Derives
        // from the font's own metrics, so different skins / fonts adapt
        // automatically.
        const int timePadRight =
            (align == QStringLiteral("right"))
                ? fontDef->hSpacing + 1
                : 0;
        int drawX = x;
        if      (align == QStringLiteral("center"))
            drawX = x + (w - lineW) / 2;
        else if (align == QStringLiteral("right"))
            drawX = x + (w - lineW - timePadRight);
        int drawY = y + (h - fontDef->charHeight) / 2;
        if (drawY < y) drawY = y;
        // For classic-style NUMBERS.BMP fonts the colon glyph isn't
        // in the bitmap at all (Winamp 2's BIGNUM had only 0–9 and
        // `-`).  Detect by sampling: if `:`'s extracted cell has no
        // bright pixels matching the digit color, paint it as two
        // small dots procedurally — the same dots a classic time
        // display draws for the colon separator.
        // Pick a representative glyph color from the digit `0`, which
        // we'll re-use for procedurally-drawn colon dots if the font
        // has no colon glyph of its own (classic NUMBERS.BMP style).
        QImage zeroGlyph = fontReg.glyph(fontId, u'0', bmpReg);
        QImage colonGlyph = fontReg.glyph(fontId, u':', bmpReg);
        // Sample the digit's dominant non-background color.  Looks for
        // any "strong" (non-grey) channel pixel; falls back to white.
        QColor dotColor(Qt::white);
        QRgb digitSample = 0;
        if (!zeroGlyph.isNull()) {
            QImage z = zeroGlyph.convertToFormat(QImage::Format_ARGB32);
            for (int yy = 0; yy < z.height() && digitSample == 0; ++yy) {
                const QRgb *row = reinterpret_cast<const QRgb *>(z.constScanLine(yy));
                for (int xx = 0; xx < z.width(); ++xx) {
                    const QRgb rgb = row[xx];
                    if (qAlpha(rgb) == 0) continue;
                    const int r = qRed(rgb), g = qGreen(rgb), b = qBlue(rgb);
                    const int mx = qMax(r, qMax(g, b));
                    const int mn = qMin(r, qMin(g, b));
                    if (mx > 80 && (mx - mn) > 30) {   // saturated, not grey
                        dotColor = QColor::fromRgb(rgb);
                        digitSample = rgb;
                        break;
                    }
                }
            }
        }
        // Detect a "blank" colon glyph: the cell exists but contains
        // no pixels matching the digit color we just sampled.  Covers
        // both empty/transparent cells and dark filler cells (where
        // alpha=255 but no glyph pixels match the digit hue).
        bool colonIsBlank = colonGlyph.isNull();
        if (!colonIsBlank && digitSample != 0) {
            QImage c = colonGlyph.convertToFormat(QImage::Format_ARGB32);
            int matchPx = 0;
            for (int yy = 0; yy < c.height() && matchPx == 0; ++yy) {
                const QRgb *row = reinterpret_cast<const QRgb *>(c.constScanLine(yy));
                for (int xx = 0; xx < c.width(); ++xx) {
                    if (qAlpha(row[xx]) == 0) continue;
                    const int r = qRed(row[xx]), g = qGreen(row[xx]), b = qBlue(row[xx]);
                    const int mx = qMax(r, qMax(g, b));
                    const int mn = qMin(r, qMin(g, b));
                    if (mx > 80 && (mx - mn) > 30) { ++matchPx; break; }
                }
            }
            if (matchPx == 0) colonIsBlank = true;
        }
        int cx = drawX;
        for (int i = 0; i < text.size(); ++i) {
            const QChar ch = text.at(i);
            if (ch == u':' && colonIsBlank) {
                // 3-wide × 1-tall dots at the left of the colon's
                // narrow cell.  Two stacked at thirds of the digit
                // cell — the classic colon-dot positions.
                const int dotH = 1;
                const int dotW = 3;
                const int dotX = cx;
                const int t1   = drawY + fontDef->charHeight / 3;
                const int t2   = drawY + (fontDef->charHeight * 2) / 3;
                p->save();
                p->setRenderHint(QPainter::Antialiasing, false);
                p->setRenderHint(QPainter::SmoothPixmapTransform, false);
                p->fillRect(QRect(dotX, t1, dotW, dotH), dotColor);
                p->fillRect(QRect(dotX, t2, dotW, dotH), dotColor);
                p->restore();
            } else {
                QImage glyph = fontReg.glyph(fontId, ch, bmpReg);
                if (!glyph.isNull())
                    p->drawImage(QPoint(cx, drawY), glyph);
            }
            cx += cellWidth(ch);
            if (i < text.size() - 1) cx += trailingSpace(ch);
        }
        p->restore();
        return true;
    }

    // ── TrueType fallback ──────────────────────────────────────
    // Wasabi's `font="Arial"` etc. — render via QPainter::drawText.
    // Honours fontsize, bold, italic, color (best-effort string→QColor),
    // align, antialias.
    //
    // Most distros don't ship the Microsoft Web Fonts that classic
    // Winamp skins assume.  Substitute the closest metrically-similar
    // open replacement so tab labels and tickers don't fall through
    // to whatever fc-match picks at random (Bitstream Vera, which
    // visibly differs from Tahoma/Arial in both stroke weight and
    // x-height).  Liberation Sans is the canonical Tahoma/Arial
    // replacement and is in Fedora's base.  Done once at startup
    // because QFont::insertSubstitution is process-global.
    static const bool fontSubsInstalled = [] {
        QFont::insertSubstitution(QStringLiteral("Tahoma"),
                                   QStringLiteral("Liberation Sans"));
        QFont::insertSubstitution(QStringLiteral("Arial"),
                                   QStringLiteral("Liberation Sans"));
        QFont::insertSubstitution(QStringLiteral("Verdana"),
                                   QStringLiteral("Liberation Sans"));
        QFont::insertSubstitution(QStringLiteral("Microsoft Sans Serif"),
                                   QStringLiteral("Liberation Sans"));
        return true;
    }();
    (void)fontSubsInstalled;
    // A skin-shipped <truetypefont id file> takes precedence over
    // treating the id as a system family name — Win9x-era skins
    // declare e.g. font="titlebar" backed by their own Tahoma Bold.
    const QString ttfFamily = fontReg.truetypeFamily(fontId);
    QFont qf(ttfFamily.isEmpty() ? fontId : ttfFamily);
    const int fontsize = attrInt(attrs, QStringLiteral("fontsize"), 12);
    // Wasabi `fontsize` is a Win32 lfHeight (character cell height),
    // whereas Qt's setPixelSize is the EM bounding box, so the two
    // don't map 1:1.  A 5/7 ratio (~0.71) approximates the conversion:
    // a bold title at fontsize=14 lands at setPixelSize(10), matching
    // the small bitmap-style display font of the classic titlebar.
    // GDI renders Arial Bold's character cell narrower than Qt does,
    // so the scale-down is needed to avoid oversized text.
    // WASABIQT_FONT_RATIO=N,D overrides the ratio for ad-hoc tuning.
    const bool isBold   = attrBool(attrs, QStringLiteral("bold"));
    const bool isItalic = attrBool(attrs, QStringLiteral("italic"));
    // Win32 lfHeight → Qt pixel size (faithful per-font mapping; replaces
    // the old 5/7 ratio that under-sized every skin's text).
    const int qpx = wasabiFontPixelSize(fontsize, fontId, isBold, isItalic);
    qf.setPixelSize(qpx);
    if (isBold)   qf.setBold(true);
    if (isItalic) qf.setItalic(true);

    // Wasabi `antialias`: "1" forces smoothing on, "0" forces it OFF
    // (crisp bitmap-style text — Bento's info-display lines set this so
    // their small fonts render sharp, not the smooth TrueType default).
    // Toggling QPainter::TextAntialiasing alone does NOT disable TrueType
    // smoothing in Qt — the QFont style strategy has to carry NoAntialias.
    // Absent → leave Qt's default untouched.
    const QString aa = attrs.value(QStringLiteral("antialias"));
    if (aa == QStringLiteral("0"))
        qf.setStyleStrategy(QFont::NoAntialias);
    else if (aa == QStringLiteral("1"))
        qf.setStyleStrategy(QFont::PreferAntialias);

    p->save();
    p->setFont(qf);
    if (aa == QStringLiteral("1"))
        p->setRenderHint(QPainter::TextAntialiasing, true);
    else if (aa == QStringLiteral("0"))
        p->setRenderHint(QPainter::TextAntialiasing, false);

    // Colour: literal "r,g,b" or a named id registered via
    // `<color id="X" gammagroup="Y" value="r,g,b"/>`.  Named ids
    // resolve through the active gammaset so Color Themes affect
    // text colour the same way they affect bitmap chrome.
    const QString colorStr = attrs.value(QStringLiteral("color"));
    QColor color(Qt::white);
    if (colors) {
        color = colors->resolve(colorStr, gammasets, QColor(Qt::white));
    } else if (colorStr.contains(QChar(','))) {
        const auto parts = colorStr.split(QChar(','));
        if (parts.size() == 3)
            color = QColor(parts[0].toInt(), parts[1].toInt(), parts[2].toInt());
    }
    p->setPen(color);

    int qFlag = Qt::AlignVCenter;
    if      (align == QStringLiteral("center")) qFlag |= Qt::AlignHCenter;
    else if (align == QStringLiteral("right"))  qFlag |= Qt::AlignRight;
    else                                        qFlag |= Qt::AlignLeft;

    const int shadowX0 = attrInt(attrs, QStringLiteral("shadowx"), 0);

    // Shadow colour (shared by both the single- and multi-line paths).
    QColor shadowColor(0, 0, 0, 180);
    const bool hasShadow = attrBool(attrs, QStringLiteral("shadow"));
    if (hasShadow) {
        const QString shadowColorStr =
            attrs.value(QStringLiteral("shadowcolor"));
        if (colors && !shadowColorStr.isEmpty()) {
            shadowColor = colors->resolve(shadowColorStr, gammasets,
                                           shadowColor);
        } else if (shadowColorStr.contains(QChar(','))) {
            const auto parts = shadowColorStr.split(QChar(','));
            if (parts.size() >= 3)
                shadowColor = QColor(parts[0].toInt(), parts[1].toInt(),
                                      parts[2].toInt());
        }
    }
    const int sx = attrInt(attrs, QStringLiteral("shadowx"), 0);
    const int sy = attrInt(attrs, QStringLiteral("shadowy"), 1);

    // Single-line text: place it by centring the actual glyph INK in the
    // widget box, then draw at an explicit baseline.  This reproduces the
    // reference, where Wasabi (GDI) centres the font cell so an all-caps
    // title sits dead-centre in the bar — Qt::AlignVCenter instead centres
    // the line box (ascent+descent+leading) and, with the substituted
    // metric-compatible font's asymmetric ascent, lifts the ink a couple
    // px high.  tightBoundingRect is the measured ink, so this self-
    // corrects for any font/size with no magic offset.  (Multi-line text
    // keeps the flag-based block centring below.)
    if (!text.contains(QLatin1Char('\n'))) {
        QFontMetrics qfm(qf);
        const QRect ink = qfm.tightBoundingRect(text);   // rel. to baseline
        const int baseline = y + (h - ink.height()) / 2 - ink.top();
        const int adv = qfm.horizontalAdvance(text);
        int tx;
        if (align == QStringLiteral("center"))
            tx = x + (w - adv) / 2;
        else if (align == QStringLiteral("right"))
            tx = x + w - adv;
        else
            tx = x + (2 - qMax(0, shadowX0));   // Wasabi left inset
        if (hasShadow) {
            const QPen savedPen = p->pen();
            p->setPen(shadowColor);
            p->drawText(tx + sx, baseline + sy, text);
            p->setPen(savedPen);
        }
        p->drawText(tx, baseline, text);
        p->restore();
        p->restore();  // outer save for clipRect
        return true;
    }

    // Multi-line fallback: centre the whole text block in the box.
    QRect drawRect(x, y, w, h);
    if ((qFlag & Qt::AlignHorizontal_Mask) == Qt::AlignLeft)
        drawRect.translate(2 - qMax(0, shadowX0), 0);
    if (hasShadow) {
        const QPen savedPen = p->pen();
        p->setPen(shadowColor);
        p->drawText(drawRect.translated(sx, sy), qFlag, text);
        p->setPen(savedPen);
    }
    p->drawText(drawRect, qFlag, text);
    p->restore();
    p->restore();  // outer save for clipRect
    return true;
}

}  // namespace qtWasabi::TextPainter
