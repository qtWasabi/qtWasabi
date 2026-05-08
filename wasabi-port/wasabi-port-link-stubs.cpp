// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// wasabi-port-link-stubs.cpp — link-time bodies for the upstream
// classes whose headers we override in include-stubs/.  These exist
// so the opensourced vcpu.cpp links into libwasabiqt; they are NOT a
// functional implementation of those classes.  WasabiQT-own code
// supplies real bodies for the methods it cares about (later
// milestones), and may either replace this file's symbols at link
// time or hide them behind weak attributes.
//
// All bodies here return harmless defaults (0 / nullptr / empty).
// Calling them at runtime won't crash but won't do anything either.

#include <api/script/scriptmgr.h>
#include <api/script/script.h>
#include <api/script/scriptobj.h>
#include <api/script/objecttable.h>
#include <api/script/objects/systemobj.h>
#include <bfc/ptrlist.h>
#include <bfc/tlist.h>
#include <bfc/nsguid.h>

// linux.h's min/max macros stomp on STL templates; undef before
// pulling unordered_map.
#ifdef min
#  undef min
#endif
#ifdef max
#  undef max
#endif

#include <cstdarg>
#include <cstdio>
#include <unordered_map>
#include <cstring>
#include <cstdlib>
#include <cwchar>

// ── ScriptObjectManager (SOM) ────────────────────────────────────
ScriptObjectManager::ScriptObjectManager()  {}
ScriptObjectManager::~ScriptObjectManager() {}

scriptVar ScriptObjectManager::makeVar(int type) {
    scriptVar v{}; v.type = type; return v;
}
scriptVar ScriptObjectManager::makeVar(int type, ScriptObject *o) {
    scriptVar v{}; v.type = type; v.data.odata = o; return v;
}

void ScriptObjectManager::assign(scriptVar *v, const wchar_t *) { if (v) v->type = SCRIPT_STRING; }
void ScriptObjectManager::assign(scriptVar *v, int i)            { if (v) { v->type = SCRIPT_INT;     v->data.idata = i; } }
void ScriptObjectManager::assign(scriptVar *v, float f)          { if (v) { v->type = SCRIPT_FLOAT;   v->data.fdata = f; } }
void ScriptObjectManager::assign(scriptVar *v, double d)         { if (v) { v->type = SCRIPT_DOUBLE;  v->data.ddata = d; } }
void ScriptObjectManager::assign(scriptVar *v, ScriptObject *o)  { if (v) { v->type = SCRIPT_OBJECT;  v->data.odata = o; } }
void ScriptObjectManager::assign(scriptVar *v1, scriptVar *v2)   { if (v1 && v2) *v1 = *v2; }
void ScriptObjectManager::assignPersistent(scriptVar *v1, scriptVar *v2) { if (v1 && v2) *v1 = *v2; }
void ScriptObjectManager::strflatassign(scriptVar *v, const wchar_t *)        { if (v) v->type = SCRIPT_STRING; }
void ScriptObjectManager::persistentstrassign(scriptVar *v, const wchar_t *)  { if (v) v->type = SCRIPT_STRING; }

// Comparison helpers — needed by `if (string == "literal")` and
// `if (n < 5)` style guards in real scripts.  A returning-zero
// stub means every guard evaluates false, which silently disables
// every behaviour predicated on a value check (including
// titlebar.m's `if (param == "padtitleright")`).
namespace {
double asDouble(const scriptVar *v) {
    if (!v) return 0.0;
    switch (v->type) {
        case SCRIPT_INT:     return v->data.idata;
        case SCRIPT_FLOAT:   return v->data.fdata;
        case SCRIPT_DOUBLE:  return v->data.ddata;
        case SCRIPT_BOOLEAN: return v->data.idata ? 1.0 : 0.0;
        default:             return 0.0;
    }
}
int compareStrings(const wchar_t *a, const wchar_t *b) {
    if (!a) a = L"";
    if (!b) b = L"";
    return wcscmp(a, b);
}
}  // namespace

int ScriptObjectManager::compEq(scriptVar *v1, scriptVar *v2) {
    if (!v1 || !v2) return 0;
    if (v1->type == SCRIPT_STRING || v2->type == SCRIPT_STRING) {
        return compareStrings(v1->data.sdata, v2->data.sdata) == 0;
    }
    return asDouble(v1) == asDouble(v2);
}
int ScriptObjectManager::compNeq(scriptVar *v1, scriptVar *v2) {
    return !compEq(v1, v2);
}
int ScriptObjectManager::compA(scriptVar *v1, scriptVar *v2) {
    if (!v1 || !v2) return 0;
    if (v1->type == SCRIPT_STRING || v2->type == SCRIPT_STRING)
        return compareStrings(v1->data.sdata, v2->data.sdata) > 0;
    return asDouble(v1) > asDouble(v2);
}
int ScriptObjectManager::compAe(scriptVar *v1, scriptVar *v2) {
    return compA(v1, v2) || compEq(v1, v2);
}
int ScriptObjectManager::compB(scriptVar *v1, scriptVar *v2) {
    if (!v1 || !v2) return 0;
    if (v1->type == SCRIPT_STRING || v2->type == SCRIPT_STRING)
        return compareStrings(v1->data.sdata, v2->data.sdata) < 0;
    return asDouble(v1) < asDouble(v2);
}
int ScriptObjectManager::compBe(scriptVar *v1, scriptVar *v2) {
    return compB(v1, v2) || compEq(v1, v2);
}

int    ScriptObjectManager::makeInt    (scriptVar *v) { return v ? v->data.idata : 0; }
float  ScriptObjectManager::makeFloat  (scriptVar *v) { return v ? v->data.fdata : 0.0f; }
double ScriptObjectManager::makeDouble (scriptVar *v) { return v ? v->data.ddata : 0.0; }
bool   ScriptObjectManager::makeBoolean(scriptVar *v) { return v && v->data.idata != 0; }
int    ScriptObjectManager::isNumeric  (scriptVar *v) {
    if (!v) return 0;
    return (v->type == SCRIPT_INT || v->type == SCRIPT_FLOAT ||
            v->type == SCRIPT_DOUBLE || v->type == SCRIPT_BOOLEAN) ? 1 : 0;
}
int ScriptObjectManager::isString(scriptVar *v) { return v && v->type == SCRIPT_STRING; }
int ScriptObjectManager::isVoid  (scriptVar *v) { return !v || v->type == SCRIPT_VOID; }
int ScriptObjectManager::isObject(scriptVar *v) { return v && v->type == SCRIPT_OBJECT; }
int ScriptObjectManager::isNumericType(int t) {
    return (t == SCRIPT_INT || t == SCRIPT_FLOAT ||
            t == SCRIPT_DOUBLE || t == SCRIPT_BOOLEAN) ? 1 : 0;
}
int  ScriptObjectManager::typeCheck(VCPUscriptVar *, int)        { return 1; }
// Per-script SystemObject — set up by SkinRuntime via the public
// `Maki::registerSystemObject(scriptId, ScriptObject*)` shim before
// each addScript().  Upstream's addScript reads this back and binds
// it as var[0], which is the load-bearing line in the whole "scripts
// can find handlers" chain (see vcpu.cpp line 452-457).
namespace {
    std::unordered_map<int, SystemObject *> g_perScriptSystem;
    std::unordered_map<int, const wchar_t *> g_perScriptParam;
    int g_currentScript = -1;
}

namespace WasabiQt::Maki {
void registerSystemObject(int scriptId, SystemObject *o) {
    if (!o) g_perScriptSystem.erase(scriptId);
    else    g_perScriptSystem[scriptId] = o;
}

void registerScriptParam(int scriptId, const wchar_t *param) {
    if (!param) g_perScriptParam.erase(scriptId);
    else        g_perScriptParam[scriptId] = param;
}

const wchar_t *currentScriptParam() {
    auto it = g_perScriptParam.find(g_currentScript);
    return it == g_perScriptParam.end() ? L"" : it->second;
}

void setCurrentScriptId(int scriptId) { g_currentScript = scriptId; }
}  // namespace

SystemObject *ScriptObjectManager::getSystemObject(int id) {
    auto it = g_perScriptSystem.find(id);
    return it == g_perScriptSystem.end() ? nullptr : it->second;
}
SystemObject *ScriptObjectManager::getSystemObjectByScriptId(int id) {
    return getSystemObject(id);
}
void ScriptObjectManager::mid(wchar_t *dest, const wchar_t *str, int s, int l) {
    if (!dest) return;
    if (!str) { dest[0] = 0; return; }
    int slen = (int)std::wcslen(str);
    if (s < 0) s = 0;
    if (s >= slen) { dest[0] = 0; return; }
    if (l < 0 || s + l > slen) l = slen - s;
    std::wmemcpy(dest, str + s, l);
    dest[l] = 0;
}

// ── Script ───────────────────────────────────────────────────────
namespace WasabiQt::Maki { void getVmState(int *vsd, int *vip, int *vsp); }

void Script::guruMeditation(SystemObject *, int code, const wchar_t *pub, int) {
    int vsd = -1, vip = -1, vsp = -1;
    WasabiQt::Maki::getVmState(&vsd, &vip, &vsp);
    std::fprintf(stderr,
                 "[wasabiqt] Maki guru meditation: code=%d sid=%d ip=%d vsp=%d",
                 code, vsd, vip, vsp);
    if (pub) std::fputws(L" pub=", stderr), std::fputws(pub, stderr);
    std::fputc('\n', stderr);
    if (const char *e = ::getenv("WASABIQT_FATAL_ASSERTS"); e && *e == '1')
        std::abort();
}

scriptVar MAKE_SCRIPT_INT(int i) {
    scriptVar v{}; v.type = SCRIPT_INT; v.data.idata = i; return v;
}

// ── ScriptObject default vtable ──────────────────────────────────
void *ScriptObject::vcpu_getInterfaceObject(GUID, ScriptObject **o) { if (o) *o = this; return this; }
int   ScriptObject::vcpu_getAssignedVariable(int, int, int, int *, int *, int *) { return -1; }
void  ScriptObject::vcpu_addAssignedVariable(int, int) {}
void  ScriptObject::vcpu_removeAssignedVariable(int, int) {}
void  ScriptObject::vcpu_setScriptId(int) {}
void  ScriptObject::vcpu_delMembers(int) {}
int   ScriptObject::vcpu_getMember(const wchar_t *, int, int) { return -1; }

// ── ObjectTable — minimum needed so CALLM doesn't break the stack ──
//
// The opensourced vcpu.cpp's CALLM handler (vcpu.cpp:1644) checks
// `if (e->ptr != NULL)` before dispatching — so a NULL ptr is safe
// IF `e->nparams` is set correctly (otherwise args don't get popped
// from the operand stack and the next opcode reads misaligned bytes).
//
// `addrefDLF` here does the bare minimum: look up a method's nparams
// in a static table of known Wasabi method signatures.  e->ptr stays
// NULL (so the call is a no-op returning 0/void), but the stack
// stays aligned — scripts run cleanly past CALLM, just with no real
// effect on widget state.  M13c/d will plug actual function pointers
// in for the named methods we care about.

namespace {
struct MethodSig { const wchar_t *name; int nparams; };

// Conservative table: every method titlebar.maki/std.mi/configtabs.maki
// can call.  nparams matches what the upstream `getExportedFunctions`
// declares for each.  Add here as new bindings are needed.
static const MethodSig kKnownMethods[] = {
    // SystemObject (https://opensourced source: api/script/objects/systemobj.cpp)
    {L"onScriptLoaded",          0},
    {L"onScriptUnloading",       0},
    {L"getRuntimeVersion",       0},
    {L"getSkinName",             0},
    {L"getParam",                0},
    {L"getToken",                3},
    {L"getScriptGroup",          0},
    {L"messageBox",              4},
    {L"getPrivateInt",           3},
    {L"setPrivateInt",           3},
    {L"getPublicInt",            3},
    {L"setPublicInt",            3},
    {L"getDate",                 0},
    {L"getTimeOfDay",            0},
    {L"isTransparencyAvailable", 0},
    {L"integerToString",         1},
    {L"stringToInteger",         1},
    {L"navigateUrlBrowser",      1},
    {L"setDelay",                0},
    {L"onTimer",                 0},
    {L"show",                    0},
    {L"hide",                    0},
    {L"stop",                    0},
    {L"getPlayItemMetaDataString", 1},
    {L"getPlayItemDisplayTitle", 0},
    {L"onLeftClick",             0},

    // GuiObject / Group / Layer / Layout / Container — most common
    {L"findObject",              1},
    {L"getObject",               1},
    {L"setVisible",              1},
    {L"getVisible",              0},
    {L"setXmlParam",             2},
    {L"getXmlParam",             1},
    {L"setAlpha",                1},
    {L"getAlpha",                0},
    {L"getAutoWidth",            0},
    {L"getAutoHeight",           0},
    {L"getWidth",                0},
    {L"getHeight",               0},
    {L"getLeft",                 0},
    {L"getTop",                  0},
    {L"clientToScreenX",         1},
    {L"clientToScreenY",         1},
    {L"screenToClientX",         1},
    {L"screenToClientY",         1},
    {L"getParent",               0},
    {L"getParentLayout",         0},
    {L"getParentGroup",          0},
    {L"bringToFront",            0},
    {L"bringToBack",             0},

    // Text — methods titlebar.maki uses on the centre text
    {L"setText",                 1},
    {L"getText",                 0},
    {L"onTextChanged",           1},
    {L"onResize",                4},

    // Common system events (params per upstream Wasabi)
    {L"onSetXuiParam",           2},
    {L"onNotify",                4},
    {L"onPlay",                  0},
    {L"onPause",                 0},
    {L"onStop",                  0},
    {L"onTitleChange",           1},
    {L"onTitle2Change",          1},

    // sentinel
    {nullptr, 0},
};

int lookupNparams(const wchar_t *name) {
    if (!name) return 0;
    for (auto *m = kKnownMethods; m->name; ++m)
        if (wcscmp(m->name, name) == 0) return m->nparams;
    return 0;          // best guess for unknowns
}

}  // namespace

// Implemented in maki-bindings.cpp — full method registry with
// real function bodies for the load-bearing Wasabi methods.
namespace WasabiQt::Maki {
struct MakiMethod { const wchar_t *name; int nparams; void *ptr; };
const MakiMethod *makiMethodTable(int *count);
}

int ObjectTable::addrefDLF(VCPUdlfEntry *dlf, int id) {
    if (!dlf) return 0;
    // Upstream: when a method's physical pointer is already
    // registered, reuse its DLFid; else assign `id` (highestDLFId)
    // and signal the caller to bump the counter.  We assign a fresh
    // id every time — `getDLFFromPointer` would let multiple scripts
    // share an existing id, but our table is small enough that the
    // duplication is harmless.
    dlf->DLFid = id;
    if (dlf->functionName) {
        int n = 0;
        const auto *t = WasabiQt::Maki::makiMethodTable(&n);
        for (int i = 0; i < n; ++i) {
            if (wcscmp(t[i].name, dlf->functionName) == 0) {
                dlf->nparams = t[i].nparams;
                dlf->ptr     = t[i].ptr;
                return 1;
            }
        }
        // Known by name only → safe no-op via nparams (e->ptr stays
        // NULL; CALLM short-circuits to int 0 with stack still aligned).
        dlf->nparams = lookupNparams(dlf->functionName);
        dlf->ptr     = nullptr;
    }
    return 1;
}
void ObjectTable::delrefDLF(VCPUdlfEntry *)                 {}
int  ObjectTable::getClassFromName(const wchar_t *)         { return -1; }
int  ObjectTable::getClassFromGuid(GUID)                    { return -1; }
const wchar_t *ObjectTable::getClassName(int)               { return L""; }
int  ObjectTable::getClassEntryIdx(int)                     { return -1; }
int  ObjectTable::isClassInstantiable(int)                  { return 0; }
int  ObjectTable::isClassReferenceable(int)                 { return 0; }
ScriptObject *ObjectTable::instantiate(int)                 { return nullptr; }
void ObjectTable::destroy(ScriptObject *)                   {}
class_entry *ObjectTable::getClassEntry(int)                { return nullptr; }

// ── SystemObject ─────────────────────────────────────────────────
int SystemObject::isObjectValid(ScriptObject *)             { return 0; }
PtrList<ScriptObject> *SystemObject::getAllScriptObjects()  { static PtrList<ScriptObject> empty; return &empty; }
TList<int> *SystemObject::getTypesList()                    { static TList<int> empty; return &empty; }
void SystemObject::setIsOldFormat(int)                       {}
int  SystemObject::isOldFormat()                             { return 0; }
int  SystemObject::isLoaded()                                { return 0; }
void SystemObject::addInstantiatedObject(ScriptObject *)    {}
void SystemObject::removeInstantiatedObject(ScriptObject *) {}
void SystemObject::onUnload()                                {}

// ── Misc helpers vcpu.cpp uses ───────────────────────────────────
wchar_t *WCSDUP(const wchar_t *s) {
    if (!s) return nullptr;
    size_t n = std::wcslen(s) + 1;
    auto *out = static_cast<wchar_t *>(std::malloc(n * sizeof(wchar_t)));
    if (out) std::wmemcpy(out, s, n);
    return out;
}

// _DebugString / _DebugStringW / StringPrintf bodies are already
// linked from upstream's bfc/string/{string,StringW}.cpp.
