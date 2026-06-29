// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once
//
// syscb-service.h — WASABI_API_SYSCB system-callback service.
//
// gen_ml registers system-event listeners (file-change watcher,
// config-change watcher, app-shutdown notifier).  The Wasabi API
// exposes Register/Deregister + Issue (broadcast).  We back the
// listener list with a vector and the broadcast with a simple
// linear-call.  ml_*'s SystemCallback implementations subclass
// against this surface.
//

#include "service-registry.h"

#include <cstdint>

namespace qtWasabi {
namespace wasabi_compat {
namespace svc {

class SysCallback {
public:
    virtual ~SysCallback() = default;
    // Wasabi SysCallback subclasses override `SysCallback_Notify`
    // with a per-event-class signature.  This is the base
    // interface; concrete subclasses arrive with their bound enums.
    virtual int  notifyEvent(uint32_t eventClass, uintptr_t param1, uintptr_t param2) = 0;
    virtual uint32_t getEventClass() const = 0;
};

class SysCbService : public ServiceObject {
public:
    GUID         guid()        const override { return SYSCB_GUID; }
    const char  *typeName()    const override { return "syscb"; }
    const char  *displayName() const override { return "qtWasabi System callbacks"; }

    void registerCallback(SysCallback *cb);
    void deregisterCallback(SysCallback *cb);

    // Broadcast `eventClass` to every registered callback that
    // declares matching `getEventClass()`.  Returns the bitwise-OR
    // of all callbacks' return codes (Wasabi convention).
    int  issueEvent(uint32_t eventClass, uintptr_t param1 = 0, uintptr_t param2 = 0);

    static SysCbService &instance() {
        static SysCbService s;
        return s;
    }
};

}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
