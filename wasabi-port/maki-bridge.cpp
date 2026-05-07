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

#include <cstdio>
#include <cstring>

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

// The link-stubs file owns the scriptId → SystemObject map.
// Forward-declare the public shim here (we're already inside the
// WasabiQt::Maki namespace block).
void registerSystemObject(int scriptId, SystemObject *o);

void registerScriptSystemObject(int scriptId, void *systemObjectHandle) {
    auto *so = static_cast<SystemObject *>(
        static_cast<ScriptObject *>(systemObjectHandle));
    registerSystemObject(scriptId, so);
}

int fireEventByName(int scriptId, const wchar_t *functionName) {
    if (!functionName) return -1;
    const int base = VCPU::dlfBase(scriptId);
    for (int i = base; i < VCPU::DLFentryTable.getNumItems(); ++i) {
        VCPUdlfEntry *e = VCPU::DLFentryTable.enumItem(i);
        if (!e || e->scriptId != scriptId) continue;
        if (e->functionName && wcscmp(e->functionName, functionName) == 0) {
            SystemObject *so = SOM::getSystemObjectByScriptId(scriptId);
            if (!so) return -1;
            scriptVar v{};
            v.type = SCRIPT_OBJECT;
            v.data.odata = so->getScriptObject();
            // Track which script is dispatching so getParam() returns
            // the right per-script param= string.
            const int prev = scriptId;        // simple — we don't nest
            setCurrentScriptId(scriptId);
            VCPU::executeEvent(v, e->DLFid, 0, scriptId);
            (void)prev;
            return e->DLFid;
        }
    }
    return -1;
}

bool fireOnSetXuiParam(int scriptId,
                       const wchar_t *name, const wchar_t *value) {
    if (!name || !value) return false;
    // Find the onSetXuiParam DLF entry.
    int dlfid = -1; int nparams = 2;
    const int base = VCPU::dlfBase(scriptId);
    for (int i = base; i < VCPU::DLFentryTable.getNumItems(); ++i) {
        VCPUdlfEntry *e = VCPU::DLFentryTable.enumItem(i);
        if (!e || e->scriptId != scriptId) continue;
        if (e->functionName &&
            wcscmp(e->functionName, L"onSetXuiParam") == 0) {
            dlfid   = e->DLFid;
            nparams = e->nparams;
            break;
        }
    }
    if (dlfid < 0) return false;

    SystemObject *so = SOM::getSystemObjectByScriptId(scriptId);
    if (!so) return false;

    scriptVar nm{}; nm.type = SCRIPT_STRING; nm.data.sdata = name;
    scriptVar vl{}; vl.type = SCRIPT_STRING; vl.data.sdata = value;
    VCPU::push(vl);
    VCPU::push(nm);

    scriptVar v{};
    v.type = SCRIPT_OBJECT;
    v.data.odata = so->getScriptObject();
    setCurrentScriptId(scriptId);
    VCPU::executeEvent(v, dlfid, /*np*/ nparams, scriptId);
    return true;
}

int dumpEvents_helper_dummy() { return 0; }
int dumpEvents(int scriptId, char *out, int outCap) {
    if (!out || outCap <= 0) return 0;
    out[0] = 0;
    int written = 0, count = 0;
    for (int i = 0; i < VCPU::eventsTable.getNumItems(); ++i) {
        VCPUeventEntry *ev = VCPU::eventsTable.enumItem(i);
        if (!ev) continue;
        if (scriptId >= 0 && ev->scriptId != scriptId) continue;
        // Find the DLF name
        const wchar_t *name = L"?";
        for (int j = 0; j < VCPU::DLFentryTable.getNumItems(); ++j) {
            auto *d = VCPU::DLFentryTable.enumItem(j);
            if (d && d->DLFid == ev->DLFid && d->scriptId == ev->scriptId) {
                if (d->functionName) name = d->functionName;
                break;
            }
        }
        char nb[64] = {0};
        for (int k = 0; k < 63 && name[k]; ++k)
            nb[k] = (name[k] < 128) ? char(name[k]) : '?';
        char buf[256];
        int n = ::snprintf(buf, sizeof(buf),
                            "ev[%d]: var=%d sid=%d dlf=%d off=%d %s\n",
                            i, ev->varId, ev->scriptId, ev->DLFid,
                            ev->pointer, nb);
        if (n < 0 || written + n + 1 >= outCap) break;
        ::memcpy(out + written, buf, n);
        written += n;
        ++count;
    }
    out[written] = 0;
    return count;
}

int dumpDlfNames(int scriptId, char *out, int outCap) {
    if (!out || outCap <= 0) return 0;
    out[0] = 0;
    int written = 0, count = 0;
    const int base = VCPU::dlfBase(scriptId);
    for (int i = base; i < VCPU::DLFentryTable.getNumItems(); ++i) {
        VCPUdlfEntry *e = VCPU::DLFentryTable.enumItem(i);
        if (!e || e->scriptId != scriptId) continue;
        if (!e->functionName) continue;
        char buf[128];
        // wcstombs is fine for ASCII method names (Wasabi's are all ASCII).
        size_t n = wcstombs(buf, e->functionName, sizeof(buf) - 1);
        if (n == size_t(-1)) continue;
        buf[n] = 0;
        if (written + (int)n + 1 >= outCap) break;
        memcpy(out + written, buf, n);
        written += n;
        out[written++] = '\n';
        ++count;
    }
    out[written] = 0;
    return count;
}

}  // namespace WasabiQt::Maki
