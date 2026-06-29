// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once
//
// palette-service.h — WASABI_API_PALETTE colour-query service.
//
// gen_ml's SkinnedScrollWnd / SkinnedListView ask for system /
// skin colours by name (e.g. "wasabi.listview.background.color",
// "wasabi.listview.text.color").  Real Wasabi merges Win32 system
// colours + the active skin's colour palette and returns COLORREF.
//
// Our backing routes through the active skin's ColorRegistry when
// one is available (set by the embedder via setBoundColors);
// falls back to sensible Win32 system defaults when not.  This is
// the bit that lets a ml.dll-hosted control inherit Bento's
// `color.ml.*` overrides automatically.
//

#include "service-registry.h"

#include "win32/windef.h"  // COLORREF

#include <QString>

namespace qtWasabi {
class ColorRegistry;
class GammasetRegistry;

namespace wasabi_compat {
namespace svc {

class PaletteService : public ServiceObject {
public:
    GUID         guid()        const override { return PALETTE_GUID; }
    const char  *typeName()    const override { return "palette"; }
    const char  *displayName() const override { return "qtWasabi Palette"; }

    // Embedder hookup.  qtamp's SkinView binds the active skin's
    // ColorRegistry + the gammasets once the skin loads.
    void setBoundColors(qtWasabi::ColorRegistry    *colors,
                         qtWasabi::GammasetRegistry *gammasets);

    // Look up a colour by name.  Tries the bound ColorRegistry
    // first; falls back to the Win32 system-colour table for
    // legacy "wasabi.sys.*" names.  Returns the supplied
    // `fallback` COLORREF when nothing matches.
    COLORREF queryColor(const QString &name, COLORREF fallback) const;

    static PaletteService &instance() {
        static PaletteService s;
        return s;
    }

private:
    qtWasabi::ColorRegistry    *m_colors    = nullptr;
    qtWasabi::GammasetRegistry *m_gammasets = nullptr;
};

}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
