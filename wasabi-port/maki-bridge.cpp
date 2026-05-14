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
#include <cstdlib>
#include <cstring>

namespace WasabiQt::Maki {

int assignNewScriptId() {
    return VCPU::assignNewScriptId();
}

// M14d: snapshot the VM's current dispatch state. Read by the assert
// handler in wasabi-port-stubs.cpp so the assertion message points at
// the actual ip/vsp/script that fired the assert, not just the file
// and line of the default switch case in the dispatch loop.
void getVmState(int *vsd, int *vip, int *vsp) {
    if (vsd) *vsd = VCPU::VSD;
    if (vip) *vip = VCPU::VIP;
    if (vsp) *vsp = VCPU::VSP;
}

// M14i: walk the script's entries in variablesTable and replace any
// null SCRIPT_OBJECT receiver with the given fallback object. _predecl
// classes from the Wasabi standard library (Config, etc.) reserve a
// variable slot but the runtime is expected to bind a singleton there.
// Without that binding every dispatch on the predecl gurus with
// GURU_NULLCALLED. This helper installs a fallback after addScript so
// the script's PUSH var[N] reads a non-null receiver and the call
// chain lands on the Config method stubs in maki-bindings.cpp.
//
// `fallback` should be a ScriptObject*, opaque here so callers do not
// need to include scriptobj.h. Returns the number of slots patched.
int hydrateNullObjectVars(int scriptId, void *fallback) {
    if (!fallback) return 0;
    int patched = 0;
    int n = VCPU::variablesTable.getNumItems();
    for (int i = 0; i < n; ++i) {
        VCPUscriptVar *v = VCPU::variablesTable.enumItem(i);
        if (!v || v->scriptId != scriptId) continue;
        // Skip primitives (SCRIPT_INT/FLOAT/DOUBLE/BOOLEAN/STRING,
        // type IDs 2..6): they store their value in the data union and
        // a null .odata represents the value 0, touching them would
        // corrupt them.  Hydrate everything else with a null pointer —
        // includes SCRIPT_OBJECT (7), class-typed slots (>=8), and the
        // sentinel -1 the loader leaves when `ObjectTable::
        // getClassFromName` returns -1 for an unrecognised class name
        // (our stub does that for everything, so SystemObject globals
        // declared as `extern System sys;` in std.mi land here unbound).
        if (v->v.type >= SCRIPT_INT && v->v.type <= SCRIPT_STRING) continue;
        if (v->v.data.odata != nullptr) continue;
        v->v.data.odata = static_cast<ScriptObject *>(fallback);
        ++patched;
    }
    if (const char *t = getenv("WASABIQT_TRACE_HYDRATE"); t && *t == '1') {
        fprintf(stderr, "[hydrate] sid=%d patched %d null object vars\n",
                scriptId, patched);
    }
    return patched;
}

// Singleton fallback ScriptObject used by hydrateNullObjectVars. Lives
// in the bindings TU (sole owner of createWidgetScriptObject) so we
// expose it through this getter rather than reach across.
extern "C" void *wq_config_dummy_get();   // defined in maki-bindings.cpp
void *getConfigDummy() { return wq_config_dummy_get(); }

int addScript(const void *blob, int blobSize, int cpuId) {
    if (!blob || blobSize <= 0) return -1;
    int sid = VCPU::addScript(const_cast<void *>(blob), blobSize, cpuId);
    if (const char *t = getenv("WASABIQT_TRACE_ADDSCRIPT"); t && *t == '1') {
        fprintf(stderr, "[addScript] cpuId=%d -> sid=%d (numScripts now %d)\n",
                cpuId, sid, VCPU::numScripts);
    }
    return sid;
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

int fireZeroArgEventOnObject(void *recv, const wchar_t *eventName) {
    if (!recv || !eventName) return 0;
    // Pre-check: does this receiver actually have any handler bound
    // for this event?  Without the pre-check, an unrelated widget
    // (e.g. a titlebar layer) consumes every click because many
    // scripts have `someOtherWidget.onLeftClick { ... }` handlers in
    // their DLF tables and executeEvent silently no-ops on receivers
    // they're not bound to.  We need to return 0 in that case so the
    // caller can fall through to its default behaviour (window drag).
    int fired = 0;
    const int n = VCPU::DLFentryTable.getNumItems();
    for (int i = 0; i < n; ++i) {
        VCPUdlfEntry *e = VCPU::DLFentryTable.enumItem(i);
        if (!e || !e->functionName) continue;
        if (wcscmp(e->functionName, eventName) != 0) continue;
        // Check if THIS receiver has a (varId, scriptId) pair in
        // eventsTable matching this DLFid — that's executeEvent's
        // inner-loop condition, but we want to know the answer
        // without firing.
        auto *wso = static_cast<ScriptObject *>(recv);
        int next = 0, evIdx = 0, inh = 0;
        int varId = wso->vcpu_getAssignedVariable(
            0, e->scriptId, e->DLFid, &next, &evIdx, &inh);
        if (varId < 0) continue;  // not bound to this receiver
        scriptVar v{};
        v.type = SCRIPT_OBJECT;
        v.data.odata = wso;
        setCurrentScriptId(e->scriptId);
        VCPU::executeEvent(v, e->DLFid, 0, e->scriptId);
        ++fired;
    }
    if (std::getenv("WASABIQT_TRACE_MAKI"))
        std::fprintf(stderr, "[maki] fireZeroArgEventOnObject(%p, %ls) -> fired=%d\n",
                     recv, eventName, fired);
    return fired;
}

int fireOnActionEvent(void *recv, const wchar_t *action,
                      const wchar_t *param, int x, int y,
                      int p1, int p2, void *source) {
    if (!recv || !action) return 0;
    int fired = 0;
    const int n = VCPU::DLFentryTable.getNumItems();
    for (int i = 0; i < n; ++i) {
        VCPUdlfEntry *e = VCPU::DLFentryTable.enumItem(i);
        if (!e || !e->functionName) continue;
        if (wcscmp(e->functionName, L"onAction") != 0) continue;
        auto *wso = static_cast<ScriptObject *>(recv);
        int next = 0, evIdx = 0, inh = 0;
        int varId = wso->vcpu_getAssignedVariable(
            0, e->scriptId, e->DLFid, &next, &evIdx, &inh);
        if (varId < 0) continue;
        // Push args in declared order — onAction's signature is
        // (action, param, x, y, p1, p2, source).
        scriptVar va{};  va.type = SCRIPT_STRING; va.data.sdata = action;
        scriptVar vp{};  vp.type = SCRIPT_STRING; vp.data.sdata = param ? param : L"";
        scriptVar vx{};  vx.type = SCRIPT_INT;    vx.data.idata = x;
        scriptVar vy{};  vy.type = SCRIPT_INT;    vy.data.idata = y;
        scriptVar vp1{}; vp1.type = SCRIPT_INT;   vp1.data.idata = p1;
        scriptVar vp2{}; vp2.type = SCRIPT_INT;   vp2.data.idata = p2;
        scriptVar vs{};  vs.type = SCRIPT_OBJECT;
        vs.data.odata = static_cast<ScriptObject *>(source);
        VCPU::push(va); VCPU::push(vp); VCPU::push(vx); VCPU::push(vy);
        VCPU::push(vp1); VCPU::push(vp2); VCPU::push(vs);

        scriptVar recvVar{};
        recvVar.type = SCRIPT_OBJECT;
        recvVar.data.odata = wso;
        setCurrentScriptId(e->scriptId);
        VCPU::executeEvent(recvVar, e->DLFid, e->nparams, e->scriptId);
        ++fired;
    }
    if (std::getenv("WASABIQT_TRACE_MAKI"))
        std::fprintf(stderr,
            "[maki] fireOnActionEvent recv=%p action=%ls fired=%d\n",
            recv, action, fired);
    return fired;
}

bool fireFourIntEvent(int scriptId, void *recv,
                      const wchar_t *eventName,
                      int a, int b, int c, int d) {
    if (!eventName || !recv) return false;
    int dlfid = -1; int nparams = 4;
    const int base = VCPU::dlfBase(scriptId);
    for (int i = base; i < VCPU::DLFentryTable.getNumItems(); ++i) {
        VCPUdlfEntry *e = VCPU::DLFentryTable.enumItem(i);
        if (!e || e->scriptId != scriptId) continue;
        if (e->functionName &&
            wcscmp(e->functionName, eventName) == 0) {
            dlfid   = e->DLFid;
            nparams = e->nparams;
            break;
        }
    }
    if (dlfid < 0) return false;
    if (const char *t = ::getenv("WASABIQT_TRACE_MAKI")) {
        char nb[64];
        const wchar_t *wn = eventName;
        int o = 0;
        for (; wn[o] && o < (int)sizeof(nb)-1; ++o)
            nb[o] = (wn[o] < 128) ? (char)wn[o] : '?';
        nb[o] = 0;
        ::fprintf(stderr,
            "[maki] fire %s sid=%d a=%d b=%d c=%d d=%d np=%d\n",
            nb, scriptId, a, b, c, d, nparams);
    }
    // Stack push order: DECL order (first arg first), so after
    // executeEvent's pop loop + runEvent's re-push, the handler's
    // top-of-stack at entry holds the LAST declared arg.  Wasabi
    // bytecode pops args in reverse-decl order, so this lines up.
    //
    // WASABIQT_PUSH_ORDER=revdecl flips this for experimentation
    // against handlers that encode the opposite convention; the four
    // shipped test skins (WinampModernPP, Winamp Modern, DeClassified,
    // Bento, Big Bento) all run zero-guru in either mode, so the
    // choice is mostly about which set of bytecode emitters expect
    // which order.
    const bool reverseDecl =
        ::getenv("WASABIQT_PUSH_ORDER") &&
        std::strcmp(::getenv("WASABIQT_PUSH_ORDER"), "revdecl") == 0;
    scriptVar va{}; va.type = SCRIPT_INT; va.data.idata = a;
    scriptVar vb{}; vb.type = SCRIPT_INT; vb.data.idata = b;
    scriptVar vc{}; vc.type = SCRIPT_INT; vc.data.idata = c;
    scriptVar vd{}; vd.type = SCRIPT_INT; vd.data.idata = d;
    if (reverseDecl) {
        VCPU::push(vd); VCPU::push(vc); VCPU::push(vb); VCPU::push(va);
    } else {
        VCPU::push(va); VCPU::push(vb); VCPU::push(vc); VCPU::push(vd);
    }

    scriptVar recvVar{};
    recvVar.type = SCRIPT_OBJECT;
    recvVar.data.odata = static_cast<ScriptObject *>(recv);
    setCurrentScriptId(scriptId);
    VCPU::executeEvent(recvVar, dlfid, /*np*/ nparams, scriptId);
    return true;
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

// M14a diagnostic: walk every codeTable entry and print its (scriptId,
// size, base) so we can tell whether there is more than one buffer per
// script (e.g., a separate code segment alongside a strings/data segment).
int dumpAllCodeBlocks(char *out, int outCap) {
    if (!out || outCap <= 0) return 0;
    out[0] = 0;
    int written = 0, count = 0;
    int n = VCPU::codeTable.getNumItems();
    written += ::snprintf(out + written, outCap - written,
                          "codeTable has %d entries\n", n);
    for (int i = 0; i < n && written + 80 < outCap; ++i) {
        auto *cb = VCPU::codeTable.enumItem(i);
        if (!cb) continue;
        int sz = 0;
        char *base = VCPU::getCodeBlock(cb->scriptId, &sz);
        written += ::snprintf(out + written, outCap - written,
                              "  cb[%d]: sid=%d size=%d base=%p (getCodeBlock returns %p, size=%d)\n",
                              i, cb->scriptId, cb->size, (void *)cb->codeBlock,
                              (void *)base, sz);
        ++count;
    }
    return count;
}

// M14a diagnostic: dump bytes from a script's codeblock so we can sanity
// check whether the codeblock pointer is sane and what bytes live around
// the offset claimed by the eventsTable.pointer field.
int dumpCodeblock(int scriptId, int offset, int nBytes, char *out, int outCap) {
    if (!out || outCap <= 0) return 0;
    out[0] = 0;
    int cbSize = 0;
    char *cb = VCPU::getCodeBlock(scriptId, &cbSize);
    if (!cb) {
        return ::snprintf(out, outCap, "codeblock(sid=%d) is NULL\n", scriptId);
    }
    int written = ::snprintf(out, outCap,
                             "codeblock(sid=%d) base=%p size=%d, dump @offset=%d:\n",
                             scriptId, (void *)cb, cbSize, offset);
    for (int i = 0; i < nBytes && offset + i < cbSize && written + 4 < outCap; ++i) {
        written += ::snprintf(out + written, outCap - written,
                              "%02x ", (unsigned char)cb[offset + i]);
        if ((i + 1) % 16 == 0 && written + 1 < outCap) {
            out[written++] = '\n';
            out[written] = 0;
        }
    }
    if (written + 1 < outCap) {
        out[written++] = '\n';
        out[written] = 0;
    }
    return written;
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
        char nb[64];
        wq_wide_to_ascii(name, nb, sizeof(nb));
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
