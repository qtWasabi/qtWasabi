#pragma once
//
// ColorRegistry — collects every `<color id="X" value="r,g,b"
// gammagroup="Y"/>` declaration from the parsed skin and resolves
// named colour tokens (e.g. "titlebar.text.color") through the
// active gammaset.
//
// Widgets in Wasabi skins frequently reference named colours by id
// rather than literal r,g,b — Color Themes swap the active gammaset
// and every gammaset-tagged colour re-tints accordingly.  Without a
// resolver TextPainter and the `<vis>` painter would render in the
// declared base colour regardless of the active theme.

#include <QColor>
#include <QHash>
#include <QString>

namespace qtWasabi {

namespace SkinXml { struct Document; }
class GammasetRegistry;

class ColorRegistry {
public:
    int  loadFromDocument(const SkinXml::Document &doc);

    // Resolve a value that may be either a literal `r,g,b` triplet
    // or a named colour id registered via `<color id=…>`.  If the
    // id resolves AND a GammasetRegistry is supplied, the colour
    // is transformed through the active gammaset's tagged group.
    // Returns `fallback` when the value isn't recognised at all.
    QColor resolve(const QString &value,
                   const GammasetRegistry *gammasets,
                   QColor fallback = QColor(255, 255, 255)) const;

    struct Entry {
        QColor  rgb;
        QString gammagroup;
    };

private:
    QHash<QString, Entry> m_colors;
};

}  // namespace qtWasabi
