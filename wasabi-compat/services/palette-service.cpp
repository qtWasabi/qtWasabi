// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "services/palette-service.h"

#include <qtWasabi/ColorRegistry.h>

#include <QColor>

namespace qtWasabi {
namespace wasabi_compat {
namespace svc {

void PaletteService::setBoundColors(qtWasabi::ColorRegistry    *colors,
                                      qtWasabi::GammasetRegistry *gammasets) {
    m_colors    = colors;
    m_gammasets = gammasets;
}

COLORREF PaletteService::queryColor(const QString &name, COLORREF fallback) const {
    if (m_colors) {
        // ColorRegistry returns QColor; convert to Win32 COLORREF
        // (BGR byte order).
        const QColor c = m_colors->resolve(name, m_gammasets,
                                             QColor(GetRValue(fallback),
                                                     GetGValue(fallback),
                                                     GetBValue(fallback)));
        return RGB(c.red(), c.green(), c.blue());
    }
    return fallback;
}

namespace {
struct AutoRegisterPalette {
    AutoRegisterPalette() { registerService(&PaletteService::instance()); }
};
static AutoRegisterPalette s_register;
}  // anonymous

}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
