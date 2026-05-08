// SPDX-License-Identifier: MIT
//
// maki-bridge.h — clean C++ surface for the opensourced VCPU class.
//
// vcpu.h pulls bfc/platform/linux.h which dumps ~30 Win32-isms onto
// the global namespace as macros (`None`, `min`, `max`, `Cursor`,
// `Font`, `RGB`, `MAX_PATH`, `TRUE`, `FALSE`, …).  These collide with
// Qt headers anywhere both are #included in the same TU.  Downstream
// code interacts with the VM through this bridge instead, which is
// safe to mix with Qt.
//
// Implementation lives in maki-bridge.cpp, which IS the only TU that
// directly includes vcpu.h.

#pragma once

#include <stdint.h>

namespace WasabiQt::Maki {

// Add a script blob to the VM.  Returns the assigned script id, or
// -1 on parse failure.  `blob` must be a complete .maki file.
int  addScript(const void *blob, int blobSize, int cpuId = 0);

// Remove a previously-added script.
void removeScript(int scriptId);

// Number of currently-loaded scripts (debug telemetry).
int  scriptCount();

}  // namespace WasabiQt::Maki
