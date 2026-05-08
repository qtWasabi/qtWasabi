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

// Reserve a new script id from the VM. Increments VCPU::numScripts.
// Call this before addScript so each script gets its own VM identity.
int  assignNewScriptId();

// Snapshot the VM dispatcher's current state into the out params.
// Used by the assert handler so failed assertions print the script
// and ip context instead of just the default switch case location.
void getVmState(int *vsd, int *vip, int *vsp);

// Add a script blob to the VM.  Returns the assigned script id, or
// -1 on parse failure.  `blob` must be a complete .maki file.
// Pass cpuId from a prior call to assignNewScriptId().
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

// Set the per-script `param=` string used by getParam() / getToken().
// Must be valid UTF-16 with lifetime past the script's lifetime —
// SkinRuntime keeps a backing QString it owns.
void registerScriptParam(int scriptId, const wchar_t *param);
const wchar_t *currentScriptParam();
void setCurrentScriptId(int scriptId);

// Walk `scriptId`'s DLF table, find the entry with the given UTF-16
// function name (e.g. L"onScriptLoaded"), and fire it via
// VCPU::executeEvent against the script's bound SystemObject.
// Returns the DLF id used, or -1 if no matching entry.
int  fireEventByName(int scriptId, const wchar_t *functionName);

// Fire System.onSetXuiParam(name, value) on `scriptId`'s SystemObject.
// In real Wasabi these are delivered to the script of an embedded
// group when its host frame instantiates with non-standard XUI tag
// attributes (`<Wasabi:MainFrame:NoStatus padtitleleft="10" .../>`).
// Returns true if a handler was found.
bool fireOnSetXuiParam(int scriptId,
                       const wchar_t *name, const wchar_t *value);

// Diagnostic: list the DLF names registered for `scriptId` (one per
// line, UTF-8) into `out`.  Returns count.
int  dumpDlfNames(int scriptId, char *out, int outCap);

// Diagnostic: list event-table entries (varId,scriptId,DLFid,ptr)
// for `scriptId`, plus the DLF name behind each.
int  dumpEvents(int scriptId, char *out, int outCap);

// Diagnostic: dump a hex slice of the script's codeblock starting at the
// given offset. Used to confirm whether the codeblock pointer is sane and
// what bytes really live at the offsets the eventsTable claims.
int  dumpCodeblock(int scriptId, int offset, int nBytes, char *out, int outCap);

// Diagnostic: walk every codeTable entry and print (scriptId, size, base).
// Tells us whether there are multiple buffers per script (e.g., a separate
// code segment alongside the strings/data segment).
int  dumpAllCodeBlocks(char *out, int outCap);

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
