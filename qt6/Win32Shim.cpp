// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Win32Shim.h"

// Translation unit anchor.  Win32Shim.h is header-only typedefs;
// keeping a .cpp here ensures the static library has at least one
// non-header symbol on platforms where empty archives confuse the
// linker (older macOS toolchains).

namespace WasabiQt::detail {
const char *win32_shim_anchor = "WasabiQT::Win32Shim";
}
