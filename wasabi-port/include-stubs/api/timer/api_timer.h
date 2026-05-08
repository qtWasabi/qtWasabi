// Stub overlay for upstream <api/timer/api_timer.h>.  Upstream's
// header has `#error port me!` for non-Win32/non-macOS, so we can't
// include it on Linux.  Provide just enough of the surface for the
// VM and scriptmgr to compile — they only forward TimerClient* and
// TimerToken handles, never call into the timer API.
#pragma once

#include <bfc/dispatch.h>

class TimerClient;

// Linux version: opaque pointer-sized handle.  The real Win32 build
// uses UINT_PTR; we don't care about the value, only that there's a
// type to forward.
using TimerToken = void *;

class timer_api : public Dispatchable {
public:
    TimerToken timer_add(TimerClient *client, intptr_t id, int ms);
    void       timer_remove(TimerClient *client, TimerToken token);

    enum {
        TIMER_API_ADD    = 1,
        TIMER_API_REMOVE = 11,
    };
};

inline TimerToken timer_api::timer_add(TimerClient *, intptr_t, int) {
    return nullptr;
}
inline void timer_api::timer_remove(TimerClient *, TimerToken) {}
