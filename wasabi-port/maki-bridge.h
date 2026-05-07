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

// Register the per-script SystemObject BEFORE calling addScript.
// Upstream's VCPU::addScript reads SOM::getSystemObjectByScriptId
// after parsing the .maki blob and binds the returned object as
// var[0] of the script (see vcpu.cpp line ~457) — without this
// binding, no event handler ever matches and scripts run nothing.
//
// `systemObjectHandle` must be a WidgetScriptObject created via
// createWidgetScriptObject(...); it'll be returned through the
// upstream SOM::getSystemObject family.
void registerScriptSystemObject(int scriptId, void *systemObjectHandle);

// Walk `scriptId`'s DLF table, find the entry with the given UTF-16
// function name (e.g. L"onScriptLoaded"), and fire it via
// VCPU::executeEvent against the script's bound SystemObject.
// Returns the DLF id used, or -1 if no matching entry.
int  fireEventByName(int scriptId, const wchar_t *functionName);

// Diagnostic: list the DLF names registered for `scriptId` (one per
// line, UTF-8) into `out`.  Returns count.
int  dumpDlfNames(int scriptId, char *out, int outCap);

// ── WidgetScriptObject ──────────────────────────────────────────
// Opaque handle to a ScriptObject instance backed by a Qt-side
// widget pointer.  The VM dispatches against this handle without
// the caller needing to include the opensourced <api/script/scriptobj.h>.
//
// `opaqueWidget` is whatever the embedder wants to store; convention
// is `Layout::ResolvedWidget *`.  All operations are O(1).

void *createWidgetScriptObject(void *opaqueWidget);
void  destroyWidgetScriptObject(void *handle);
void *opaqueOf(void *handle);
int   scriptIdOf(void *handle);

}  // namespace WasabiQt::Maki
