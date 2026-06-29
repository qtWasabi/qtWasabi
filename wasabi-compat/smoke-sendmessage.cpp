// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// smoke-sendmessage.cpp — smoke test for the
// SendMessage dispatcher.  Registers a TraceWindow that records
// the last (msg, wp, lp) tuple it received, fires SendMessage at
// it, and asserts the tuple round-tripped through the dispatcher.
// Also exercises GetWindowLongPtrW / SetWindowLongPtrW round-trip
// since gen_ml writes its `mainwndState *` into GWL_USERDATA.
//

#include "win32/handle-registry.h"
#include "win32/winuser.h"

#include "smoke-check.h"
#include <memory>

namespace qtWasabi {
namespace wasabi_compat {
namespace {

class TraceWindow : public WindowObject {
public:
    UINT   last_msg = 0;
    WPARAM last_wp  = 0;
    LPARAM last_lp  = 0;
    LRESULT wndProc(UINT msg, WPARAM wp, LPARAM lp) override {
        last_msg = msg;
        last_wp  = wp;
        last_lp  = lp;
        // Return a recognisable value so the SendMessage caller can
        // verify the return path.  Real wndProcs return per-message
        // semantics; for the test we use a constant fingerprint.
        return static_cast<LRESULT>(0xCAFEBABE);
    }
};

struct SendMessageSmoke {
    SendMessageSmoke() {
        auto tw = std::make_unique<TraceWindow>();
        TraceWindow *raw = tw.get();
        HWND h = registerHandle<WindowObject>(std::move(tw));
        SMOKE_CHECK(h);

        // SendMessage round-trip.
        LRESULT rv = SendMessageW(h, WM_USER + 5,
                                    static_cast<WPARAM>(0x1234),
                                    static_cast<LPARAM>(0x5678));
        SMOKE_CHECK(rv == static_cast<LRESULT>(0xCAFEBABE));
        SMOKE_CHECK(raw->last_msg == (WM_USER + 5));
        SMOKE_CHECK(raw->last_wp  == 0x1234);
        SMOKE_CHECK(raw->last_lp  == 0x5678);

        // GWL_USERDATA round-trip.
        SetWindowLongPtrW(h, GWL_USERDATA, 0xDEADBEEF);
        SMOKE_CHECK(GetWindowLongPtrW(h, GWL_USERDATA) ==
                static_cast<LONG_PTR>(0xDEADBEEF));

        // IsWindow on valid handle is true, false after destroy.
        SMOKE_CHECK(IsWindow(h));
        DestroyWindow(h);
        SMOKE_CHECK(!IsWindow(h));

        // PostMessage / SendMessage to dead handle returns 0 / TRUE
        // and does not crash.
        LRESULT rv2 = SendMessageW(h, WM_USER, 0, 0);
        SMOKE_CHECK(rv2 == 0);
    }
};
static SendMessageSmoke s_smoke;

}  // anonymous
}  // namespace wasabi_compat
}  // namespace qtWasabi
