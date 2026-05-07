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
// embedder tells WasabiQT which is active via setActiveGammaset().

#include <QtCore/qglobal.h>
#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>

namespace WasabiQt {
namespace SkinXml { struct Document; }

struct GammaGroup {
    int r = 0, g = 0, b = 0;
    int gray = 0;
    int boost = 0;
};

struct Gammaset {
    QString name;
    QHash<QString, GammaGroup> groups;        // by gammagroup id
};

class GammasetRegistry {
public:
    GammasetRegistry() = default;

    int loadFromDocument(const SkinXml::Document &doc);

    QStringList         names()      const { return m_sets.keys(); }
    const Gammaset     *find(const QString &name) const;
    const Gammaset     *active()     const { return m_active; }
    void                setActiveGammaset(const QString &name);

    // Look up the gammagroup's transform within the active gammaset.
    // Returns identity (zeros) if the active set doesn't define one.
    GammaGroup          transformFor(const QString &gammagroup) const;

    // Apply the transform IN PLACE to `img` (must be ARGB32 or
    // ARGB32_Premultiplied).  Identity transforms early-out.
    static void applyToImage(QImage &img, const GammaGroup &t);

private:
    QHash<QString, Gammaset> m_sets;
    const Gammaset          *m_active = nullptr;
};

}  // namespace WasabiQt
