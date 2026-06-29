// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once
//
// wnd-service.h — WASABI_API_WND minimal stub.
//
// gen_ml calls into the window service for:
//   * `Wnd_RegisterClassEx()` — register a custom WNDCLASSEX for
//     SkinnedListView / SkinnedScrollWnd / SkinnedButton.  In our
//     world the wasabi-compat HWND registry knows about every
//     window class implicitly; the registration is a no-op.
//   * `Wnd_GetUserData() / Wnd_SetUserData()` — covered by the
//     existing GetWindowLongPtr / SetWindowLongPtr path.
//   * `getRootWnd()` — return the main HWND.  Our caller is the
//     compat layer itself; we hand back a synthetic
//     "no-root-yet" handle for now.
//
// Returns success for every operation so plugin init paths don't
// branch on errors.
//

#include "service-registry.h"

namespace qtWasabi {
namespace wasabi_compat {
namespace svc {

class WndService : public ServiceObject {
public:
    GUID         guid()        const override { return WND_GUID; }
    const char  *typeName()    const override { return "wnd"; }
    const char  *displayName() const override { return "qtWasabi Window helpers"; }

    int  registerClassEx(const void * /*wcex*/) { return 1; }
    void *getRootWnd()                          { return nullptr; }

    static WndService &instance() {
        static WndService s;
        return s;
    }
};

}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
