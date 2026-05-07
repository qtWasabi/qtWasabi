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

int ScriptObjectManager::compEq (scriptVar *, scriptVar *) { return 0; }
int ScriptObjectManager::compNeq(scriptVar *, scriptVar *) { return 0; }
int ScriptObjectManager::compA  (scriptVar *, scriptVar *) { return 0; }
int ScriptObjectManager::compAe (scriptVar *, scriptVar *) { return 0; }
int ScriptObjectManager::compB  (scriptVar *, scriptVar *) { return 0; }
int ScriptObjectManager::compBe (scriptVar *, scriptVar *) { return 0; }

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
namespace { std::unordered_map<int, SystemObject *> g_perScriptSystem; }

namespace WasabiQt::Maki {
void registerSystemObject(int scriptId, SystemObject *o) {
    if (!o) g_perScriptSystem.erase(scriptId);
    else    g_perScriptSystem[scriptId] = o;
}
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
void Script::guruMeditation(SystemObject *, int code, const wchar_t *pub, int) {
    std::fprintf(stderr, "[wasabiqt] Maki guru meditation: code=%d", code);
    if (pub) std::fputws(L" pub=", stderr), std::fputws(pub, stderr);
    std::fputc('\n', stderr);
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

// ── ObjectTable ──────────────────────────────────────────────────
int  ObjectTable::addrefDLF(VCPUdlfEntry *, int)            { return 0; }
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
