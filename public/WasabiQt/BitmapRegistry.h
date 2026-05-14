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
#include <QList>
#include <QPoint>
#include <QRect>
#include <QString>

namespace WasabiQt {
namespace SkinXml { struct Document; }
class GammasetRegistry;

// Pairing of a `sysregion="-N"` cutout bitmap to its sibling chrome
// bitmap, with the offset at which the cutout was painted relative to
// the chrome.  When the renderer is about to paint the chrome bitmap,
// the cutout is overlaid with CompositionMode_DestinationOut so the
// chrome's alpha channel already carries the cut shape — drawing the
// chrome on top of another group's chrome (drawer on top of player at
// the overlap) leaves the underlying chrome visible at the cut pixels.
struct ChromeCutout {
    QString cutoutImage;
    QPoint  offset;
};

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

    // Install the chrome→cutout pairing extracted from the resolved
    // tree.  Subsequent `chromeImageFor()` calls return a cached copy
    // of the chrome bitmap with each cutout overlaid at its layer-
    // relative offset via CompositionMode_DestinationOut.
    void    setChromeCutouts(const QHash<QString, QList<ChromeCutout>> &cutouts);

    // Same as imageFor, but applies chrome cutouts (if any are
    // registered for this id) so the returned bitmap already carries
    // its sysregion-shaped alpha.  TreePainter uses this for chrome
    // layers; sysregion-cutout layers and Window-Region builders
    // continue to use the raw imageFor.
    QImage  chromeImageFor(const QString &id);

private:
    QHash<QString, BitmapDef> m_defs;
    QHash<QString, QImage>    m_imgCache;     // by file path (whole image)
    QHash<QString, QImage>    m_subCache;     // by bitmap id (post-tint)
    QHash<QString, QImage>    m_maskedCache;  // by chrome id (post-cutout)
    QHash<QString, QList<ChromeCutout>> m_chromeCutouts;
    QString                   m_skinDir;
    GammasetRegistry         *m_gammasets = nullptr;
};

}  // namespace WasabiQt
