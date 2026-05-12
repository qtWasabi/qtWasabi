// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/TextPainter.h>
#include <WasabiQt/FontRegistry.h>
#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/ColorRegistry.h>
#include <WasabiQt/GammasetRegistry.h>

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QSize>
#include <QString>

namespace WasabiQt::TextPainter {

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
    const QString fontId = attrs.value(QStringLiteral("font"));
    if (fontId.isEmpty()) return false;

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
        // Wasabi's BIGNUM time-display convention (measured against
        // upstream DeClassified at 275×116):
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
            // the gap to the next digit (verified against upstream's
            // pixel positions).
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
        // "trailing hSpacing after the last char" that Wasabi's
        // upstream Text::onPaint adds before measuring against the
        // widget's right edge.  Derives from the font's own metrics,
        // so different skins / fonts adapt automatically.
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
        // small dots procedurally — the same dots stock Wasabi/WACUP
        // draws for time displays.
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
                // cell — exactly where the upstream reference paints
                // them.
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
    QFont qf(fontId);
    const int fontsize = attrInt(attrs, QStringLiteral("fontsize"), 12);
    // Wasabi `fontsize` is a Win32 lfHeight (character cell height);
    // Qt's setPixelSize is the EM bounding box.  Pixel-sampled
    // against the WACUP reference (354x164 native): bold "WACUP"
    // glyphs span 37 px at fontsize=14, which Qt setPixelSize(10)
    // hits exactly — a 5/7 ratio.  Earlier 4/7 came from a deleted
    // hand-tuned C++ titlebar's setPixelSize(8) magic for Win32 GDI's
    // narrower character-cell rendering of Arial Bold.
    const int qpx = qMax(1, (fontsize * 5 + 3) / 7);
    qf.setPixelSize(qpx);
    if (attrBool(attrs, QStringLiteral("bold")))   qf.setBold(true);
    if (attrBool(attrs, QStringLiteral("italic"))) qf.setItalic(true);

    p->save();
    p->setFont(qf);
    if (attrBool(attrs, QStringLiteral("antialias")))
        p->setRenderHint(QPainter::TextAntialiasing, true);

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

    // Wasabi's Text::onPaint draws left-aligned text at
    // `r.left + 2 - shadowx + lpadding` (Src/Wasabi/api/skin/widgets/
    // text.cpp:804).  The +2 inset is what gives the titlebar's
    // WACUP its breathing room from the left streak.  Apply only
    // for left-aligned text — Qt's natural centre/right alignment
    // already handles the others.
    QRect drawRect(x, y, w, h);
    if ((qFlag & Qt::AlignHorizontal_Mask) == Qt::AlignLeft) {
        const int shadowX = attrInt(attrs, QStringLiteral("shadowx"), 0);
        drawRect.translate(2 - qMax(0, shadowX), 0);
    }
    p->drawText(drawRect, qFlag, text);
    p->restore();
    p->restore();  // outer save for clipRect
    return true;
}

}  // namespace WasabiQt::TextPainter
