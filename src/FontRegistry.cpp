// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/FontRegistry.h>
#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/SkinXml.h>

#include <QChar>
#include <QDir>
#include <QImage>
#include <QImageReader>
#include <QPoint>

namespace WasabiQt {

namespace {

void collectFonts(const SkinXml::Element &el,
                  QHash<QString, BitmapFontDef> &out) {
    if (el.tag == QStringLiteral("bitmapfont")) {
        BitmapFontDef d;
        d.id          = el.attrs.value(QStringLiteral("id"));
        d.bitmapId    = el.attrs.value(QStringLiteral("file"));
        d.charWidth   = el.attrs.value(QStringLiteral("charwidth")).toInt();
        d.charHeight  = el.attrs.value(QStringLiteral("charheight")).toInt();
        d.hSpacing    = el.attrs.value(QStringLiteral("hspacing")).toInt();
        d.vSpacing    = el.attrs.value(QStringLiteral("vspacing")).toInt();
        if (!d.id.isEmpty() && !d.bitmapId.isEmpty())
            out.insert(d.id, d);
    }
    for (const auto &c : el.children) collectFonts(c, out);
}

}  // namespace

int FontRegistry::loadFromDocument(const SkinXml::Document &doc) {
    m_defs.clear();
    m_charTableCache.clear();
    m_skinDir = doc.skinDir;
    collectFonts(doc.root, m_defs);
    return m_defs.size();
}

const BitmapFontDef *FontRegistry::find(const QString &id) const {
    auto it = m_defs.constFind(id);
    return it == m_defs.constEnd() ? nullptr : &it.value();
}

// Direct port of upstream Wasabi BitmapFont::getXYfromChar (see
// `Src/Wasabi/api/font/bitmapfont.cpp`).  Glyphs sit in 3 rows:
//   row 0 (y=0)              — A..Z (lowercase maps in), space
//   row 1 (y=charHeight)     — 0-9 ., :, (, ), -, ', etc.
//   row 2 (y=charHeight*2)   — fallback / extras
// `c` is the column index multiplied by charWidth to give x.
QPoint FontRegistry::glyphCoord(QChar ic, int charWidth, int charHeight) {
    int c = 30;        // default: undefined → space-ish slot
    int c2 = 0;
    const ushort u = ic.unicode();

    // Quick relocations: accented latin letters fold to their base.
    auto lower = ic.toLower().unicode();
    auto upper = ic.toUpper().unicode();

    if (u <= u'Z' && u >= u'A')      c = u - u'A';
    else if (u <= u'z' && u >= u'a') c = u - u'a';
    else if (u == u' ')              c = 30;
    else {
        c2 = charHeight;
        if (u == 1)                       c = 10;
        else if (u == u'.')               c = 11;
        else if (u <= u'9' && u >= u'0')  c = u - u'0';
        else if (u == u':')               c = 12;
        else if (u == u'(')               c = 13;
        else if (u == u')')               c = 14;
        else if (u == u'-')               c = 15;
        else if (u == u'\'' || u == u'`') c = 16;
        else if (u == u'!')               c = 17;
        else if (u == u'_')               c = 18;
        else if (u == u'+')               c = 19;
        else if (u == u'\\')              c = 20;
        else if (u == u'/')               c = 21;
        else if (u == u'[' || u == u'{' || u == u'<') c = 22;
        else if (u == u']' || u == u'}' || u == u'>') c = 23;
        else if (u == u'~' || u == u'^') c = 24;
        else if (u == u'&')              c = 25;
        else if (u == u'%')              c = 26;
        else if (u == u',')              c = 27;
        else if (u == u'=')              c = 28;
        else if (u == u'$')              c = 29;
        else if (u == u'#')              c = 30;
        else {
            c2 = charHeight * 2;
            // umlauts (ä/Ä → 0, ö/Ö → 1, ü/Ü → 2)
            if      (u == 0x00E4 || u == 0x00C4) c = 0;
            else if (u == 0x00F6 || u == 0x00D6) c = 1;
            else if (u == 0x00FC || u == 0x00DC) c = 2;
            else if (u == u'?')                  c = 3;
            else if (u == u'*')                  c = 4;
            else {
                c2 = 0;
                if (u == u'"')      c = 26;
                else if (u == u'@') c = 27;
                else                c = 30;
            }
        }
    }
    (void)lower; (void)upper;
    return QPoint(c * charWidth, c2);
}

QImage FontRegistry::glyph(const QString &fontId, QChar ch,
                           BitmapRegistry &bmpReg) {
    const auto *def = find(fontId);
    if (!def) return {};
    if (def->charWidth <= 0 || def->charHeight <= 0) return {};

    // Classic Winamp NUMBERS.BMP fonts (used by DeClassified et al.)
    // pack glyphs sequentially in a single row instead of Wasabi's
    // 3-row table: digits 0..9 → cells 0..9, `:` → cell 10, `-` →
    // cell 11.  The bitmap is still 3-rows-tall (Wasabi font format
    // requires it), but only one row has glyphs — which is exactly
    // what DeClassified's `skin/numfont.png` ships.
    auto classicCellIndex = [](QChar ic) -> int {
        const ushort u = ic.unicode();
        if (u >= u'0' && u <= u'9') return u - u'0';
        if (u == u':') return 10;
        if (u == u'-') return 11;  // some skins use 11; others 15
        return -1;
    };

    QImage table = m_charTableCache.value(fontId);
    if (table.isNull()) {
        // Two conventions for <bitmapfont file="X">:
        //   1) Modern style: X is a <bitmap id="..."/> registered in
        //      the BitmapRegistry (typically "bitmapfont.<font>").
        //   2) Classic style: X is a relative file path to the image
        //      (e.g. "skin/numfont.png"), no <bitmap> indirection.
        // Try registry first; fall back to loading from the skin dir.
        table = bmpReg.imageFor(def->bitmapId);
        if (table.isNull() && !m_skinDir.isEmpty()) {
            QImageReader r(QDir(m_skinDir).filePath(def->bitmapId));
            table = r.read();
        }
        if (table.isNull()) return {};
        m_charTableCache.insert(fontId, table);
    }
    auto cellHasPixels = [&](const QRect &rr) {
        if (rr.width() <= 0 || rr.height() <= 0) return false;
        QImage cell = table.copy(rr);
        if (cell.format() != QImage::Format_ARGB32 &&
            cell.format() != QImage::Format_ARGB32_Premultiplied)
            cell = cell.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < cell.height(); ++y) {
            const QRgb *row = reinterpret_cast<const QRgb *>(cell.constScanLine(y));
            for (int x = 0; x < cell.width(); ++x)
                if (qAlpha(row[x]) > 0) return true;
        }
        return false;
    };

    const QPoint p = glyphCoord(ch, def->charWidth, def->charHeight);
    QRect r(p.x(), p.y(), def->charWidth, def->charHeight);
    r = r.intersected(table.rect());
    // If the Wasabi-default cell is empty, fall back to the classic
    // NUMBERS.BMP layout (digits at 0..9, `:` at 10, `-` at 11) which
    // a few non-Modern skins author against.
    if (!r.isEmpty() && !cellHasPixels(r)) {
        const int classic = classicCellIndex(ch);
        if (classic >= 0) {
            QRect alt(classic * def->charWidth, def->charHeight,
                      def->charWidth, def->charHeight);
            alt = alt.intersected(table.rect());
            if (!alt.isEmpty() && cellHasPixels(alt)) r = alt;
        }
    }
    if (r.isEmpty()) return {};
    return table.copy(r);
}

}  // namespace WasabiQt
