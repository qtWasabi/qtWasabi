// SPDX-License-Identifier: MIT
//
// maki-bridge.cpp — sole TU that #include's the opensourced VCPU header.
//
// vcpu.h drags bfc/platform/linux.h's macro pollution (None, min,
// max, Cursor, Font, RGB, ...) into whatever TU includes it.  This
// file forwards the calls and limits the blast radius — the public
// maki-bridge.h is a clean Qt-compatible surface.

#include <api/script/vcpu.h>
#include "maki-bridge.h"

namespace WasabiQt::Maki {

int addScript(const void *blob, int blobSize, int cpuId) {
    if (!blob || blobSize <= 0) return -1;
    return VCPU::addScript(const_cast<void *>(blob), blobSize, cpuId);
}

void removeScript(int scriptId) {
    if (scriptId >= 0) VCPU::removeScript(scriptId);
}

int scriptCount() {
    return VCPU::numScripts;
}

bool runOnScriptLoaded(int /*scriptId*/, void * /*widgetObjectHandle*/) {
    // M13b stub.  Real impl needs to:
    //   1) iterate the script's eventTable
    //   2) for each entry, look up the DLF entry's functionName
    //   3) match against L"onScriptLoaded"
    //   4) push the receiver scriptVar
    //   5) call VCPU::runCode(scriptId, e->pointer, /*np*/ 0)
    // The opensourced VCPU exposes static state but not per-script
    // accessors for eventTable/dlfTable; we either expose them
    // via friend/extern access or maintain a parallel cache during
    // addScript by parsing the .maki blob ourselves.
    return false;
}

int dumpDlfNames(int /*scriptId*/, char *out, int outCap) {
    if (outCap > 0) out[0] = 0;
    return 0;
}

}  // namespace WasabiQt::Maki
