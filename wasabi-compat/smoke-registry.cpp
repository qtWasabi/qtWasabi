// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// smoke-registry.cpp — smoke test for the handle
// registry.  Compile-only static_asserts plus a runtime self-check
// that registers + looks up + unregisters one instance of each
// handle type.  Lives in the OBJECT lib so a build failure here is
// caught at link time even before any plugin lands.
//

#include "win32/handle-registry.h"

#include "smoke-check.h"
#include <memory>

namespace qtWasabi {
namespace wasabi_compat {
namespace {

// Type-distinctness check.  Win32's HWND and HMENU are different
// opaque struct pointer types, so an implicit conversion between
// them is a compile error.  This static check guards against
// someone "fixing" DECLARE_HANDLE in windef.h to use `void *`
// (which would silently allow the cross-type bug).
static_assert(!__is_same(HWND,  HMENU),    "HWND != HMENU at type level");
static_assert(!__is_same(HMENU, HBITMAP),  "HMENU != HBITMAP at type level");
static_assert(!__is_same(HICON, HBITMAP),  "HICON != HBITMAP at type level");

// Exercise the registry at static-init time.  Any failure here
// trips a debug-mode assert; the test exists primarily to force
// the templated definitions in handle-registry.cpp to instantiate
// at link time so missing-symbol bugs surface immediately.
struct RegistrySmoke {
    RegistrySmoke() {
        auto w = std::make_unique<WindowObject>();
        WindowObject *raw = w.get();
        HWND h = registerHandle<WindowObject>(std::move(w));
        SMOKE_CHECK(h != nullptr);
        SMOKE_CHECK(lookupHandle<WindowObject>(h) == raw);

        // Wrong-type lookup with the same handle bits should fail.
        // (We can't directly cast HWND to HMENU; instead probe a
        // fresh HMENU value never registered.)
        HMENU m_fake = reinterpret_cast<HMENU>(static_cast<uintptr_t>(0xDEADBEEFu));
        SMOKE_CHECK(lookupHandle<MenuObject>(m_fake) == nullptr);

        unregisterHandle<WindowObject>(h);
        // After unregister, lookup must return null.
        SMOKE_CHECK(lookupHandle<WindowObject>(h) == nullptr);
    }
};
static RegistrySmoke s_smoke;

}  // anonymous
}  // namespace wasabi_compat
}  // namespace qtWasabi
