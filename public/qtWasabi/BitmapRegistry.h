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
#include <QSet>
#include <QString>

namespace qtWasabi {
namespace SkinXml { struct Document; }
class GammasetRegistry;
class Widget;

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
    QString gammagroup; // optional — when set, the bitmap is tinted at load time using the bound gammaset
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

    // Tag every bitmap that has no gammagroup of its own with a SYNTHETIC
    // role group (syn.background / syn.button / syn.slider / syn.vis /
    // syn.text / syn.display / syn.other), inferred from how the widget
    // tree uses it and from its id.  This is what lets the synthetic
    // per-role Color Themes recolour a skin that tagged nothing itself —
    // the role is read from the skin's own structure, no guesswork.
    void                 assignRolesFromWidgetTree(const Widget &root,
                                                   bool accentDrawers = false);

    // Every distinct (non-empty) gammagroup the registered bitmaps use —
    // the skin's own names plus any synthetic roles just assigned.  The
    // synthetic theme generator turns each into a role-based transform.
    QStringList          usedGammagroups() const;

    // Recolour the BRIGHT pixels (>= `threshold` luminance) inside `rect`
    // of bitmap `id` to `color` — a masked tint that picks out a shape
    // baked into a larger bitmap (e.g. a logo painted into the body) without
    // colouring the rectangle around it.  Applied after the gammaset tint.
    void                 setAccentRegion(const QString &id, const QRect &rect,
                                         QRgb color, int threshold);

    // Tint the BRIGHT pixels (>= `threshold` luminance) inside `rect` of
    // bitmap `id` using `gammagroup`'s active transform instead of the
    // bitmap's own group — picks a set of glyphs baked into a larger bitmap
    // (e.g. a label painted into a body panel) and themes them by a different
    // role than the panel around them (so they can match a sibling widget
    // that uses that role).  Applied after the bitmap's own gammaset tint.
    void                 setRegionGroup(const QString &id, const QRect &rect,
                                        const QString &gammagroup, int threshold);
    void                 clearAccentRegions();
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

    // Runtime registration — used by ported `gen_ml` / ml_* plugins
    // whose resource BMPs ship inside the plugin rather than the skin
    // XML.  Skin lookups (`imageFor("id")`) search the XML-declared
    // `m_defs` table first; if absent there, they fall through to
    // `m_runtime` which holds plugin-supplied entries.  Plugins
    // typically register at static-init via a tiny convenience macro
    // hooked into the embedder's bootstrap.
    //
    // Two overloads trade decode timing for caller convenience:
    //   * `registerRuntimeBitmap(id, QImage&)` — direct upload, the
    //     plugin already has decoded pixels (e.g. from a baked-in
    //     resource section).
    //   * `registerRuntimeBitmap(id, filePath)` — defer decode, the
    //     plugin points us at a file on disk.  Useful when a plugin
    //     ships its resources as loose `.bmp`/`.png` files under
    //     `${dataDir}/qtWasabi/ml/icons/…`.
    //
    // Re-registering an id replaces the previous entry.  Returns
    // FALSE if the supplied bytes are empty / the path doesn't
    // resolve.  Caller-side error handling is typically "ignore" —
    // a missing icon falls back to a grey placeholder downstream.
    bool registerRuntimeBitmap(const QString &id, const QImage &img);
    bool registerRuntimeBitmap(const QString &id, const QString &filePath);
    void unregisterRuntimeBitmap(const QString &id);

private:
    QHash<QString, BitmapDef> m_defs;
    QHash<QString, QImage>    m_imgCache;     // by file path (whole image)
    QHash<QString, QImage>    m_subCache;     // by bitmap id (post-tint)
    QHash<QString, QImage>    m_maskedCache;  // by chrome id (post-cutout)
    QHash<QString, QList<ChromeCutout>> m_chromeCutouts;
    QHash<QString, QImage>    m_runtime;      // plugin-registered, decoded
    QHash<QString, QString>   m_runtimePaths; // plugin-registered, deferred decode
    QString                   m_skinDir;
    GammasetRegistry         *m_gammasets = nullptr;
    struct AccentRegion { QRect rect; QRgb color; int threshold; };
    QHash<QString, AccentRegion> m_accentRegions;  // by bitmap id
    struct RegionGroup { QRect rect; QString group; int threshold; };
    QHash<QString, QList<RegionGroup>> m_regionGroups;  // by bitmap id
    QSet<QString> m_satMaskedIds;  // tint only the saturated pixels of these
};

}  // namespace qtWasabi
