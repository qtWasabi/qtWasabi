// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/FontRegistry.h>
#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/SkinXml.h>

#include <QChar>
#include <QImage>
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

    QImage table = m_charTableCache.value(fontId);
    if (table.isNull()) {
        table = bmpReg.imageFor(def->bitmapId);
        if (table.isNull()) return {};
        m_charTableCache.insert(fontId, table);
    }
    const QPoint p = glyphCoord(ch, def->charWidth, def->charHeight);
    QRect r(p.x(), p.y(), def->charWidth, def->charHeight);
    r = r.intersected(table.rect());
    if (r.isEmpty()) return {};
    return table.copy(r);
}

}  // namespace WasabiQt
