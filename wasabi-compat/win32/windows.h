// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#ifndef _WINDOWS_H
#define _WINDOWS_H
//
// windows.h — Win32 umbrella header.  In the real SDK this pulls in
// the entire user-mode API surface (windef.h, winbase.h, wingdi.h,
// winuser.h, …).  For wasabi-compat we provide a minimum-viable
// subset that gen_ml + ml_* upstream code expects when it writes
// `#include <windows.h>` at the top of every TU.
//
// Each sub-header is a real file under `wasabi-compat/win32/`; the
// shim grows as more API surface lands.
//

// Same dependency order the Win32 SDK uses, so callers that include
// only `<windows.h>` get the full standard surface ml_* plugins assume.
#include "basetsd.h"
#include "windef.h"
#include "winbase.h"   // CRITICAL_SECTION, atoms, kernel surface
#include "winuser.h"
#include "wingdi.h"

#endif  // _WINDOWS_H
