#pragma once
//
// GammasetRegistry — collects every <gammaset> + <gammagroup>
// declaration from the parsed skin and applies the per-channel RGB
// linear transform Wasabi calls "gamma".
//
// Each <gammagroup> declares (R, G, B, gray, boost) values:
//
//   value="r,g,b"   — per-channel linear adjustment in -4096..+4096
//                     range.  rm = 65535 + r*16, applied as
//                     channel_out = clamp(channel_in * rm / 65535, 0, 255)
//   gray="0|1|2"    — desaturation strategy: 1=max(R,G,B), 2=avg
//   boost="0|1"     — pre-shift each channel toward 1.0 by alpha
//
// The transform mirrors upstream Winamp's GammaFilter::filterBitmap.
//
// A skin can ship many gammasets ("Default", "Good Ol' Winamp", ...).
// The user picks one via Color Themes drawer at runtime.  The
// embedder tells qtWasabi which is active via setActiveGammaset().

#include <QtCore/qglobal.h>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>

namespace qtWasabi {
namespace SkinXml { struct Document; }

struct GammaGroup {
    int r = 0, g = 0, b = 0;
    int gray = 0;
    int boost = 0;
};

struct Gammaset {
    QString name;
    QHash<QString, GammaGroup> groups;        // by gammagroup id
    // A "global recolor" theme applies ONE transform to every bitmap and
    // colour regardless of gammagroup tagging.  This is how a skin that
    // ships no Color Themes of its own (and tags nothing) can still be
    // recoloured — the per-group machinery has nothing to bite on, so the
    // transform is applied globally instead.
    bool       global = false;
    GammaGroup globalXform;
    // Absolute chrome palette for qtamp's own Qt dialogs/menus when this
    // (synthetic) theme is active.  A skin whose COLOURS carry no
    // gammagroup can't have its dialog tinted by the per-group transform,
    // so a synthetic theme supplies the dialog/menu colours directly —
    // letting it re-theme Preferences just like a native Color Theme does.
    bool       hasChrome = false;
    QColor     chromeBg, chromeText, chromeField, chromeFieldText,
               chromeSelBg, chromeSelText, chromeBorder, chromeButtonText;
};

class GammasetRegistry {
public:
    GammasetRegistry() = default;

    int loadFromDocument(const SkinXml::Document &doc);

    QStringList         names()      const { return m_sets.keys(); }
    const Gammaset     *find(const QString &name) const;

    // The skin's own default theme — "*Default"/"Default" if it ships one,
    // else empty (meaning "no tint", the skin's raw colours).  Used by the
    // Preferences picker's "Default colors" entry to revert.
    QString             defaultThemeName() const;
    const Gammaset     *active()     const { return m_active; }
    void                setActiveGammaset(const QString &name);

    // Look up the gammagroup's transform within the active gammaset.
    // Returns identity (zeros) if the active set doesn't define one.
    GammaGroup          transformFor(const QString &gammagroup) const;

    // Add a synthetic "global recolor" gammaset that applies `xform` to
    // every bitmap/colour when active.  Safe to call at any time (it
    // re-resolves the active set by name so the pointer stays valid).
    void                injectGlobalTheme(const QString &name,
                                          const GammaGroup &xform);

    // The active theme's global transform, or identity for a normal
    // per-gammagroup theme.  The bitmap and colour paths apply this on
    // top of (or instead of) any per-group transform.
    GammaGroup          globalTransform() const;

    // True when this skin shipped no Color Themes of its own — the embedder
    // uses this to decide whether to synthesize per-role recolor themes.
    bool                hadNoNativeThemes() const { return m_hadNoNativeThemes; }

    // True once the synthetic per-role themes have been injected for this
    // skin (so the embedder only does it once per load).
    bool                hasSyntheticStyles() const { return m_injectedSynthetic; }

    // Build the synthetic per-role recolor themes (Winamp Classic / Modern /
    // Grayscale) for a theme-less skin, mapping each of the skin's own
    // gammagroup names — plus the synthetic roles the engine tagged — to a
    // role-appropriate transform.  `groupNames` is every gammagroup the
    // skin's bitmaps actually use (BitmapRegistry::usedGammagroups()).
    void                injectSyntheticThemes(const QStringList &groupNames);

    // Apply the transform IN PLACE to `img` (must be ARGB32 or
    // ARGB32_Premultiplied).  Identity transforms early-out.  When
    // `chromaMin > 0`, pixels whose chroma (max-min channel) is below it
    // are left untouched — used to recolour the saturated parts of a
    // bitmap (a coloured panel) while preserving its neutral parts (silver
    // speaker cones).
    static void applyToImage(QImage &img, const GammaGroup &t,
                             int chromaMin = 0);

private:
    QHash<QString, Gammaset> m_sets;
    const Gammaset          *m_active = nullptr;
    bool                     m_hadNoNativeThemes = false;
    bool                     m_injectedSynthetic = false;
};

}  // namespace qtWasabi
