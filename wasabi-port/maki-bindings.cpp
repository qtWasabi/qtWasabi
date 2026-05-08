// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// maki-bindings.cpp — function bodies for Wasabi script methods.
//
// The opensourced vcpu.cpp's CALLM dispatcher (vcpu.cpp:1656+) casts
// `e->ptr` to a function-pointer signature that depends on the
// number of params:
//
//   nparams=0 → scriptVar fn(maki_cmd*, int vsd, ScriptObject*)
//   nparams=1 → scriptVar fn(maki_cmd*, int vsd, ScriptObject*, scriptVar)
//   nparams=2 → scriptVar fn(maki_cmd*, int vsd, ScriptObject*,
//                              scriptVar, scriptVar)
//   ... up to nparams=10
//
// scriptVar is passed BY VALUE (not pointer).  Each function gets the
// receiver as `__o`, the args, and returns scriptVar.
//
// Scope: M13d intentionally implements just enough that the load-
// bearing scripts (std.mi versionCheck, titlebar.maki onScriptLoaded)
// run cleanly without aborting on void returns.  Most methods return
// "neutral" values (empty string, 0, void receiver) — visible widget
// state still comes from the static `runKnownScripts` titlebar hack.
// Real bodies for the geometry methods (getAutoWidth, setXmlParam,
// findObject) land as we incrementally remove that hack.

#include <api/script/scriptobj.h>
#include <api/script/objecttable.h>
#include <api/script/vcputypes.h>

#ifdef min
#  undef min
#endif
#ifdef max
#  undef max
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <unordered_map>

namespace {

// ── scriptVar factory helpers (mirrors upstream MAKE_SCRIPT_* /
// objcontroller.cpp).  Defined locally to avoid pulling another
// upstream object file. ─────────────────────────────────────────

inline scriptVar makeVoid()              { scriptVar v{}; v.type = SCRIPT_VOID;   return v; }
inline scriptVar makeInt(int i)          { scriptVar v{}; v.type = SCRIPT_INT;    v.data.idata = i; return v; }
inline scriptVar makeFloat(float f)      { scriptVar v{}; v.type = SCRIPT_FLOAT;  v.data.fdata = f; return v; }
inline scriptVar makeDouble(double d)    { scriptVar v{}; v.type = SCRIPT_DOUBLE; v.data.ddata = d; return v; }
inline scriptVar makeBoolean(int b)      { scriptVar v{}; v.type = SCRIPT_BOOLEAN;v.data.idata = b ? 1 : 0; return v; }
inline scriptVar makeObject(ScriptObject *o) { scriptVar v{}; v.type = SCRIPT_OBJECT; v.data.odata = o; return v; }
inline scriptVar makeString(const wchar_t *s) {
    scriptVar v{}; v.type = SCRIPT_STRING; v.data.sdata = s ? s : L""; return v;
}

// ── string interning ────────────────────────────────────────────
// scriptVar sdata is `const wchar_t *` — the lifetime must outlive
// the call.  Interning keeps strings around for the process life.
std::unordered_map<std::wstring, const wchar_t *> g_intern;
const wchar_t *intern(const std::wstring &s) {
    auto it = g_intern.find(s);
    if (it != g_intern.end()) return it->second;
    wchar_t *copy = static_cast<wchar_t *>(std::malloc((s.size() + 1) * sizeof(wchar_t)));
    std::wmemcpy(copy, s.c_str(), s.size() + 1);
    g_intern.emplace(s, copy);
    return copy;
}

// ── helper: scriptVar → string ──────────────────────────────────
std::wstring vargstr(const scriptVar &v) {
    if (v.type == SCRIPT_STRING && v.data.sdata) return v.data.sdata;
    if (v.type == SCRIPT_INT)    return std::to_wstring(v.data.idata);
    return {};
}

}  // namespace

// ── method bodies (organised by class) ───────────────────────────

// SystemObject

extern "C" scriptVar wq_getRuntimeVersion(maki_cmd *, int, ScriptObject *) {
    if (std::getenv("WASABIQT_TRACE_MAKI"))
        std::fprintf(stderr, "[maki] getRuntimeVersion → 5.0\n");
    return makeDouble(5.0);
}

extern "C" scriptVar wq_getSkinName(maki_cmd *, int, ScriptObject *) {
    return makeString(L"WasabiQT");
}

extern "C" scriptVar wq_getDate(maki_cmd *, int, ScriptObject *) {
    return makeInt(0);
}

extern "C" scriptVar wq_getTimeOfDay(maki_cmd *, int, ScriptObject *) {
    return makeInt(0);
}

extern "C" scriptVar wq_isTransparencyAvailable(maki_cmd *, int, ScriptObject *) {
    return makeBoolean(1);
}

// Forward-declared in maki-bridge.h; bodies live in
// wasabi-port-link-stubs.cpp.  Declared here as plain forward so we
// don't pull the full maki-bridge.h (which exposes Qt-incompatible
// types via its Qt-side accessors).
namespace WasabiQt::Maki {
    const wchar_t *currentScriptParam();
    void setCurrentScriptId(int);
}

extern "C" scriptVar wq_getParam(maki_cmd *, int /*vsd*/, ScriptObject *) {
    // Per-script `<script param="…">` — looked up by the currently-
    // dispatching script id.  SkinRuntime sets it via setCurrentScriptId
    // before each fireEventByName / fireOnSetXuiParam call.
    return makeString(WasabiQt::Maki::currentScriptParam());
}

extern "C" scriptVar wq_getToken(maki_cmd *, int, ScriptObject *,
                                  scriptVar src, scriptVar sep, scriptVar n) {
    const std::wstring s = vargstr(src);
    const std::wstring d = vargstr(sep);
    int idx = (n.type == SCRIPT_INT) ? n.data.idata : 0;
    if (s.empty() || d.empty()) return makeString(L"");
    size_t start = 0;
    int i = 0;
    while (start <= s.size()) {
        size_t end = s.find(d[0], start);
        if (end == std::wstring::npos) end = s.size();
        if (i == idx) {
            return makeString(intern(s.substr(start, end - start)));
        }
        if (end == s.size()) break;
        start = end + 1;
        ++i;
    }
    return makeString(L"");
}

extern "C" scriptVar wq_stringToInteger(maki_cmd *, int, ScriptObject *,
                                         scriptVar s) {
    if (s.type != SCRIPT_STRING || !s.data.sdata) return makeInt(0);
    return makeInt(static_cast<int>(std::wcstol(s.data.sdata, nullptr, 10)));
}

extern "C" scriptVar wq_integerToString(maki_cmd *, int, ScriptObject *,
                                         scriptVar i) {
    if (i.type != SCRIPT_INT) return makeString(L"0");
    wchar_t buf[32];
    std::swprintf(buf, 32, L"%d", i.data.idata);
    return makeString(intern(std::wstring(buf)));
}

extern "C" scriptVar wq_messageBox(maki_cmd *, int, ScriptObject *,
                                    scriptVar /*text*/, scriptVar /*title*/,
                                    scriptVar /*flags*/, scriptVar /*tag*/) {
    return makeInt(0);
}

extern "C" scriptVar wq_navigateUrlBrowser(maki_cmd *, int, ScriptObject *,
                                            scriptVar) {
    return makeVoid();
}

extern "C" scriptVar wq_getPrivateInt(maki_cmd *, int, ScriptObject *,
                                       scriptVar /*sec*/, scriptVar /*key*/,
                                       scriptVar def) {
    return def;
}

extern "C" scriptVar wq_setPrivateInt(maki_cmd *, int, ScriptObject *,
                                       scriptVar, scriptVar, scriptVar) {
    return makeVoid();
}

extern "C" scriptVar wq_getPublicInt(maki_cmd *, int, ScriptObject *,
                                      scriptVar, scriptVar, scriptVar def) {
    return def;
}

extern "C" scriptVar wq_setPublicInt(maki_cmd *, int, ScriptObject *,
                                      scriptVar, scriptVar, scriptVar) {
    return makeVoid();
}

extern "C" scriptVar wq_getScriptGroup(maki_cmd *, int, ScriptObject *o) {
    // Returns the script's enclosing group.  For M13d, just return
    // the receiver (the SystemObject) — findObject on it walks every
    // widget.  The widget tree integration lands in M13e.
    return makeObject(o);
}

extern "C" scriptVar wq_getPlayItemMetaDataString(maki_cmd *, int,
                                                    ScriptObject *, scriptVar) {
    return makeString(L"");
}

extern "C" scriptVar wq_getPlayItemDisplayTitle(maki_cmd *, int, ScriptObject *) {
    return makeString(L"");
}

extern "C" scriptVar wq_setDelay(maki_cmd *, int, ScriptObject *) {
    return makeVoid();
}

extern "C" scriptVar wq_show(maki_cmd *, int, ScriptObject *) { return makeVoid(); }
extern "C" scriptVar wq_hide(maki_cmd *, int, ScriptObject *) { return makeVoid(); }
extern "C" scriptVar wq_stop(maki_cmd *, int, ScriptObject *) { return makeVoid(); }

// GuiObject / Group / Layer / Layout / Container — geometry stubs.
// Real widget integration in M13e.

// Qt-side bridge accessors — see src/SkinRuntimeBridge.cpp.
extern "C" {
    void *wq_widget_findById(const wchar_t *id);
    void  wq_widget_setAttr(void *handle, const wchar_t *name, const wchar_t *value);
    const wchar_t *wq_widget_getAttr(void *handle, const wchar_t *name);
    int   wq_widget_getAttrInt(void *handle, const wchar_t *name);
}

extern "C" scriptVar wq_findObject(maki_cmd *, int, ScriptObject *,
                                    scriptVar id) {
    if (id.type != SCRIPT_STRING || !id.data.sdata)
        return makeObject(nullptr);
    void *handle = wq_widget_findById(id.data.sdata);
    if (std::getenv("WASABIQT_TRACE_MAKI")) {
        char nb[128] = {0};
        for (int i = 0; i < 127 && id.data.sdata[i]; ++i)
            nb[i] = (id.data.sdata[i] < 128) ? char(id.data.sdata[i]) : '?';
        std::fprintf(stderr, "[maki] findObject(%s) -> %p\n", nb, handle);
    }
    return makeObject(static_cast<ScriptObject *>(handle));
}

extern "C" scriptVar wq_getObject(maki_cmd *, int, ScriptObject *,
                                   scriptVar id) {
    return wq_findObject(nullptr, 0, nullptr, id);
}

extern "C" scriptVar wq_setVisible(maki_cmd *, int, ScriptObject *, scriptVar) {
    return makeVoid();
}

extern "C" scriptVar wq_getVisible(maki_cmd *, int, ScriptObject *) {
    return makeBoolean(1);
}

extern "C" scriptVar wq_setXmlParam(maki_cmd *, int, ScriptObject *o,
                                     scriptVar name, scriptVar value) {
    if (!o) return makeVoid();
    if (name.type != SCRIPT_STRING || !name.data.sdata) return makeVoid();
    const wchar_t *val = (value.type == SCRIPT_STRING && value.data.sdata)
                            ? value.data.sdata : L"";
    if (std::getenv("WASABIQT_TRACE_MAKI")) {
        // %ls fprintf needs the right locale set, which we don't
        // touch.  Manual narrow conversion (ASCII-only attr/value
        // names) keeps the trace safe.
        char nb[128] = {0}, vb[256] = {0};
        for (int i = 0; i < 127 && name.data.sdata[i]; ++i)
            nb[i] = (name.data.sdata[i] < 128)
                        ? char(name.data.sdata[i]) : '?';
        for (int i = 0; val && i < 255 && val[i]; ++i)
            vb[i] = (val[i] < 128) ? char(val[i]) : '?';
        std::fprintf(stderr, "[maki] setXmlParam(%s, %s)\n", nb, vb);
    }
    wq_widget_setAttr(o, name.data.sdata, val);
    return makeVoid();
}

extern "C" scriptVar wq_getXmlParam(maki_cmd *, int, ScriptObject *o,
                                     scriptVar name) {
    if (!o || name.type != SCRIPT_STRING || !name.data.sdata)
        return makeString(L"");
    return makeString(wq_widget_getAttr(o, name.data.sdata));
}

extern "C" scriptVar wq_setAlpha(maki_cmd *, int, ScriptObject *, scriptVar) {
    return makeVoid();
}

extern "C" scriptVar wq_getAlpha(maki_cmd *, int, ScriptObject *) {
    return makeInt(255);
}

extern "C" scriptVar wq_getAutoWidth(maki_cmd *, int, ScriptObject *o)  {
    // Wasabi convention: text widgets return their measured rendered
    // text width.  For non-text widgets, falls back to declared `w`.
    // M13e returns the resolved `w` attribute as a first approximation;
    // text-specific measurement lands when we route into TextPainter.
    if (!o) return makeInt(0);
    return makeInt(wq_widget_getAttrInt(o, L"w"));
}
extern "C" scriptVar wq_getAutoHeight(maki_cmd *, int, ScriptObject *o) {
    if (!o) return makeInt(0);
    return makeInt(wq_widget_getAttrInt(o, L"h"));
}
extern "C" scriptVar wq_getWidth(maki_cmd *, int, ScriptObject *o)      {
    if (!o) return makeInt(0);
    return makeInt(wq_widget_getAttrInt(o, L"w"));
}
extern "C" scriptVar wq_getHeight(maki_cmd *, int, ScriptObject *o)     {
    if (!o) return makeInt(0);
    return makeInt(wq_widget_getAttrInt(o, L"h"));
}
extern "C" scriptVar wq_getLeft(maki_cmd *, int, ScriptObject *o)       {
    if (!o) return makeInt(0);
    return makeInt(wq_widget_getAttrInt(o, L"x"));
}
extern "C" scriptVar wq_getTop(maki_cmd *, int, ScriptObject *o)        {
    if (!o) return makeInt(0);
    return makeInt(wq_widget_getAttrInt(o, L"y"));
}

extern "C" scriptVar wq_clientToScreenX(maki_cmd *, int, ScriptObject *, scriptVar x) { return x; }
extern "C" scriptVar wq_clientToScreenY(maki_cmd *, int, ScriptObject *, scriptVar y) { return y; }
extern "C" scriptVar wq_screenToClientX(maki_cmd *, int, ScriptObject *, scriptVar x) { return x; }
extern "C" scriptVar wq_screenToClientY(maki_cmd *, int, ScriptObject *, scriptVar y) { return y; }

extern "C" scriptVar wq_getParent(maki_cmd *, int, ScriptObject *o)        { return makeObject(o); }
extern "C" scriptVar wq_getParentLayout(maki_cmd *, int, ScriptObject *o)  { return makeObject(o); }
extern "C" scriptVar wq_getParentGroup(maki_cmd *, int, ScriptObject *o)   { return makeObject(o); }

extern "C" scriptVar wq_bringToFront(maki_cmd *, int, ScriptObject *) { return makeVoid(); }
extern "C" scriptVar wq_bringToBack(maki_cmd *, int, ScriptObject *)  { return makeVoid(); }

extern "C" scriptVar wq_setText(maki_cmd *, int, ScriptObject *, scriptVar) { return makeVoid(); }
extern "C" scriptVar wq_getText(maki_cmd *, int, ScriptObject *)            { return makeString(L""); }

// ── Config / ConfigItem / ConfigAttribute (M14g stub layer) ─────
//
// The Wasabi script API exposes a Config singleton for managing
// preferences.  initAttribs() chains dozens of Config.newItem() and
// .newAttribute() calls before the rest of the script runs, and every
// missing method on that chain fires a guru meditation.  We do not
// have a real preference store yet, so all of these return a single
// shared dummy ScriptObject that survives dispatch.  setData / getData
// on it become no-ops.  Real Config plumbing is its own milestone.

// Forward-declared from widget-script-object.cpp, in the same namespace.
namespace WasabiQt::Maki { void *createWidgetScriptObject(void *); }

static ScriptObject *configDummy() {
    static ScriptObject *sentinel = static_cast<ScriptObject *>(
        WasabiQt::Maki::createWidgetScriptObject(nullptr));
    return sentinel;
}

// Exposed for the bridge so M14i hydration can fill null SCRIPT_OBJECT
// vars with this fallback. Same singleton as configDummy().
extern "C" void *wq_config_dummy_get() {
    return static_cast<void *>(configDummy());
}

extern "C" scriptVar wq_newItem(maki_cmd *, int, ScriptObject *,
                                 scriptVar, scriptVar) {
    return makeObject(configDummy());
}
extern "C" scriptVar wq_getItem(maki_cmd *, int, ScriptObject *, scriptVar) {
    return makeObject(configDummy());
}
extern "C" scriptVar wq_newAttribute(maki_cmd *, int, ScriptObject *,
                                      scriptVar, scriptVar) {
    return makeObject(configDummy());
}
extern "C" scriptVar wq_setData(maki_cmd *, int, ScriptObject *, scriptVar) {
    return makeVoid();
}
extern "C" scriptVar wq_getData(maki_cmd *, int, ScriptObject *) {
    return makeString(L"");
}

// ── method registry ─────────────────────────────────────────────

namespace WasabiQt::Maki {

struct MakiMethod { const wchar_t *name; int nparams; void *ptr; };

// Looked up by name in the new addrefDLF.  Entries with ptr=nullptr
// fall through to the safe no-op path (e->ptr stays NULL, CALLM
// returns int 0).
const MakiMethod *makiMethodTable(int *count) {
    static const MakiMethod kMethods[] = {
        // SystemObject
        {L"getRuntimeVersion",       0, (void *)wq_getRuntimeVersion},
        {L"getSkinName",             0, (void *)wq_getSkinName},
        {L"getDate",                 0, (void *)wq_getDate},
        {L"getTimeOfDay",            0, (void *)wq_getTimeOfDay},
        {L"isTransparencyAvailable", 0, (void *)wq_isTransparencyAvailable},
        {L"getParam",                0, (void *)wq_getParam},
        {L"getToken",                3, (void *)wq_getToken},
        {L"stringToInteger",         1, (void *)wq_stringToInteger},
        {L"StringToInteger",         1, (void *)wq_stringToInteger},
        {L"integerToString",         1, (void *)wq_integerToString},
        {L"IntegerToString",         1, (void *)wq_integerToString},
        {L"messageBox",              4, (void *)wq_messageBox},
        {L"navigateUrlBrowser",      1, (void *)wq_navigateUrlBrowser},
        {L"getPrivateInt",           3, (void *)wq_getPrivateInt},
        {L"setPrivateInt",           3, (void *)wq_setPrivateInt},
        {L"getPublicInt",            3, (void *)wq_getPublicInt},
        {L"setPublicInt",            3, (void *)wq_setPublicInt},
        {L"getScriptGroup",          0, (void *)wq_getScriptGroup},
        {L"getPlayItemMetaDataString", 1, (void *)wq_getPlayItemMetaDataString},
        {L"getPlayItemDisplayTitle", 0, (void *)wq_getPlayItemDisplayTitle},
        {L"setDelay",                0, (void *)wq_setDelay},
        {L"show",                    0, (void *)wq_show},
        {L"hide",                    0, (void *)wq_hide},
        {L"stop",                    0, (void *)wq_stop},
        // GuiObject family
        {L"findObject",              1, (void *)wq_findObject},
        {L"getObject",               1, (void *)wq_getObject},
        {L"setVisible",              1, (void *)wq_setVisible},
        {L"getVisible",              0, (void *)wq_getVisible},
        {L"setXmlParam",             2, (void *)wq_setXmlParam},
        {L"getXmlParam",             1, (void *)wq_getXmlParam},
        {L"setAlpha",                1, (void *)wq_setAlpha},
        {L"getAlpha",                0, (void *)wq_getAlpha},
        {L"getAutoWidth",            0, (void *)wq_getAutoWidth},
        {L"getAutoHeight",           0, (void *)wq_getAutoHeight},
        {L"getWidth",                0, (void *)wq_getWidth},
        {L"getHeight",               0, (void *)wq_getHeight},
        {L"getLeft",                 0, (void *)wq_getLeft},
        {L"getTop",                  0, (void *)wq_getTop},
        {L"clientToScreenX",         1, (void *)wq_clientToScreenX},
        {L"clientToScreenY",         1, (void *)wq_clientToScreenY},
        {L"screenToClientX",         1, (void *)wq_screenToClientX},
        {L"screenToClientY",         1, (void *)wq_screenToClientY},
        {L"getParent",               0, (void *)wq_getParent},
        {L"getParentLayout",         0, (void *)wq_getParentLayout},
        {L"getParentGroup",          0, (void *)wq_getParentGroup},
        {L"bringToFront",            0, (void *)wq_bringToFront},
        {L"bringToBack",             0, (void *)wq_bringToBack},
        {L"setText",                 1, (void *)wq_setText},
        {L"getText",                 0, (void *)wq_getText},
        // Config / ConfigItem / ConfigAttribute stubs (M14g)
        {L"newItem",                 2, (void *)wq_newItem},
        {L"getItem",                 1, (void *)wq_getItem},
        {L"newAttribute",            2, (void *)wq_newAttribute},
        {L"setData",                 1, (void *)wq_setData},
        {L"getData",                 0, (void *)wq_getData},
    };
    if (count) *count = sizeof(kMethods) / sizeof(kMethods[0]);
    return kMethods;
}

}  // namespace WasabiQt::Maki
