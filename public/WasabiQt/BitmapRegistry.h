#pragma once
//
// BitmapRegistry — collects every `<bitmap id=… file=… x=… y=… w=… h=…/>`
// definition the skin XML declares, lazy-loads the underlying QImages,
// and exposes the resolved sub-rect of each named bitmap.
//
// Wasabi skins reference bitmaps by id everywhere downstream (in
// <layer image=…/>, <button image=…/>, <text font=…/>, etc.).  This
// registry is the lookup table.

#include <QtCore/qglobal.h>
#include <QHash>
#include <QImage>
#include <QRect>
#include <QString>

namespace WasabiQt {
namespace SkinXml { struct Document; }
class GammasetRegistry;

struct BitmapDef {
    QString id;
    QString file;       // path relative to the skin root
    QRect   srcRect;    // {0,0,0,0} ⇒ whole-image (resolved at first load)
    QString gammagroup; // optional — applied at load time once gammasets land
};

class BitmapRegistry {
public:
    BitmapRegistry() = default;

    // Walk the parsed document, register every <bitmap> element.
    // Returns the number of definitions ingested.
    int loadFromDocument(const SkinXml::Document &doc);

    int                  count()        const { return m_defs.size(); }
    bool                 has(const QString &id) const { return m_defs.contains(id); }
    const BitmapDef     *find(const QString &id) const;
    QStringList          ids()          const { return m_defs.keys(); }
    const QHash<QString, BitmapDef> &all() const { return m_defs; }

    // Returns a QImage of the bitmap's sub-rect (or a null QImage if
    // the file is missing / the rect is out of bounds).  Cached after
    // first call.  When a `GammasetRegistry` was bound, bitmaps with
    // a `gammagroup=` attribute are tinted on first load.
    QImage  imageFor(const QString &id);

    // Same, but for an arbitrary BitmapDef (not in the registry).
    QImage  imageFor(const BitmapDef &def);

    // Bind a gammaset registry — tints bitmaps with a known
    // `gammagroup=` on first load, using the registry's active set.
    // Pass nullptr to disable.  Clears the image cache so subsequent
    // lookups re-tint.
    void    setGammasetRegistry(GammasetRegistry *gs);

private:
    QHash<QString, BitmapDef> m_defs;
    QHash<QString, QImage>    m_imgCache;     // by file path (whole image)
    QHash<QString, QImage>    m_subCache;     // by bitmap id (post-tint)
    QString                   m_skinDir;
    GammasetRegistry         *m_gammasets = nullptr;
};

}  // namespace WasabiQt
