#pragma once
//
// FontRegistry — collects every <bitmapfont id="…" file="…"
// charwidth=… charheight=… hspacing=… vspacing=…/> definition the
// skin XML declares, and exposes glyph-extraction for a given char.
//
// Wasabi bitmap fonts are 3-row tables in a single bitmap:
//   row 0: A..Z (case-insensitive — lowercase maps to upper) + space
//   row 1: digits, punctuation
//   row 2: umlauts, "?", "*", a few extras
// Each glyph is `charwidth` × `charheight`, with `hspacing`/`vspacing`
// added between glyphs.  The underlying bitmap is itself referenced
// via a regular <bitmap id="…" file="…"/> def in the BitmapRegistry,
// usually with id "bitmapfont.<font>" pattern.

#include <QtCore/qglobal.h>
#include <QHash>
#include <QImage>
#include <QString>

namespace WasabiQt {
namespace SkinXml { struct Document; }

class BitmapRegistry;

struct BitmapFontDef {
    QString id;
    QString bitmapId;       // resolves through BitmapRegistry
    int     charWidth   = 0;
    int     charHeight  = 0;
    int     hSpacing    = 0;
    int     vSpacing    = 0;
};

class FontRegistry {
public:
    FontRegistry() = default;

    int loadFromDocument(const SkinXml::Document &doc);

    int                       count() const { return m_defs.size(); }
    const BitmapFontDef       *find(const QString &id) const;
    const QHash<QString, BitmapFontDef> &all() const { return m_defs; }

    // Wasabi's char→(x,y) offset within the font bitmap.  Mirrors
    // BitmapFont::getXYfromChar from the upstream source.  Returns
    // QPoint(x, y) measured in pixels into the source bitmap.
    static QPoint glyphCoord(QChar ch, int charWidth, int charHeight);

    // Extract a single glyph for `ch` from the font's underlying
    // bitmap.  Pulls the source through `bmpReg` once and caches.
    QImage glyph(const QString &fontId, QChar ch, BitmapRegistry &bmpReg);

private:
    QHash<QString, BitmapFontDef> m_defs;
    QHash<QString, QImage>        m_charTableCache;  // by font id
};

}  // namespace WasabiQt
