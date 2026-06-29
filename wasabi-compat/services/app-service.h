// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once
//
// app-service.h — WASABI_API_APP minimal implementation.
//
// gen_ml + ml_* call into the application service to query host
// metadata: install path, executable directory, DPI scale, version,
// "main HWND", and the cooperative-shutdown vote
// (`main_cancelShutdown()`).
//
// We back each query with a sensible Qt-derived default:
//   * getPath / getNamedString — `QStandardPaths` (writable app
//     data, executable dir, …)
//   * DPI scale — qApp's primary screen logical/physical DPI
//   * version — qtWasabi version string
//   * shutdown vote — no-op (qtamp has no cooperative-shutdown
//     handshake, so the vote has no effect)
//

#include "service-registry.h"

namespace qtWasabi {
namespace wasabi_compat {
namespace svc {

class AppService : public ServiceObject {
public:
    GUID         guid()        const override { return APP_GUID; }
    const char  *typeName()    const override { return "app"; }
    const char  *displayName() const override { return "qtWasabi Application"; }

    // In the Wasabi API each of these is reached via the Dispatchable
    // opcode set and returns a wide-string. We expose a thin C++ API
    // that the Dispatchable bridge forwards to.

    // 0 — install path; 1 — executable dir; 2 — userdata
    // (matches the wa.h `IPC_GETINISTRINGW` index family).
    const wchar_t *getPath(int which);

    // Logical DPI scale, e.g. 1.0 on a regular screen, 2.0 on a
    // HiDPI display.
    double         getDpiScale();

    // Application version string ("qtWasabi 0.1").
    const wchar_t *getVersionString();

    // Returns FALSE — qtamp doesn't currently expose a
    // cooperative-shutdown path, so plugins voting against
    // shutdown have no effect.
    int            mainCancelShutdown();

    static AppService &instance() {
        static AppService s;
        return s;
    }
};

}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
