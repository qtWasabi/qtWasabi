// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// maki-bindings.cpp — function bodies for Wasabi script methods.
//
// The Maki VM's CALLM dispatcher casts `e->ptr` to a function-pointer
// signature that depends on the number of params:
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
// Most methods return "neutral" values (empty string, 0, void receiver)
// where a real body is not yet warranted; the geometry methods
// (getAutoWidth, setXmlParam, findObject) carry full implementations.

#include <api/script/scriptobj.h>
#include <api/script/objecttable.h>
#include <api/script/vcputypes.h>

// Class registry + instance-class stamps (maki-classes.cpp /
// widget-script-object.cpp) — the typed-object bodies below mint
// Region-classed results and key teardown off the Timer class.
namespace qtWasabi::Maki {
int  makiClassIndexFromName(const wchar_t *name);
void setScriptObjectClass(void *handle, int classIdx);
}

#ifdef min
#  undef min
#endif
#ifdef max
#  undef max
#endif

#include <map>
#include <vector>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cmath>
#include <cwctype>
#include <ctime>
#include <functional>
#include <string>
#include <unordered_map>

namespace {

// ── scriptVar factory helpers (mirror the Maki MAKE_SCRIPT_*
// constructors).  Defined locally so this TU stays self-contained.
// ─────────────────────────────────────────────────────────────────

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

// scriptVar -> int, honouring the arithmetic-double / float / string tags
// the VCPU may leave in the slot.  Used by the string/conversion builtins.
int vargint(const scriptVar &v) {
    switch (v.type) {
        case SCRIPT_INT:
        case SCRIPT_BOOLEAN: return v.data.idata;
        case SCRIPT_FLOAT:   return static_cast<int>(v.data.fdata);
        case SCRIPT_DOUBLE:  return static_cast<int>(v.data.ddata);
        case SCRIPT_STRING:  return v.data.sdata
                                 ? static_cast<int>(std::wcstol(v.data.sdata, nullptr, 10)) : 0;
        default:             return 0;
    }
}
double vargdouble(const scriptVar &v) {
    switch (v.type) {
        case SCRIPT_INT:
        case SCRIPT_BOOLEAN: return v.data.idata;
        case SCRIPT_FLOAT:   return v.data.fdata;
        case SCRIPT_DOUBLE:  return v.data.ddata;
        case SCRIPT_STRING:  return v.data.sdata ? std::wcstod(v.data.sdata, nullptr) : 0.0;
        default:             return 0.0;
    }
}

}  // namespace

// ── method bodies (organised by class) ───────────────────────────

// SystemObject

extern "C" scriptVar wq_getRuntimeVersion(maki_cmd *, int, ScriptObject *) {
    // Faithful value: Maki returns MAKE_SCRIPT_DOUBLE(MAKI_RUNTIME_VERSION)
    // = 2.  Skins are authored against 2; a value like 5.0 could make a
    // `getRuntimeVersion() >= N` gate take an unintended branch.
    return makeDouble(2.0);
}

// Bridge-owned name of the loaded skin (SkinRuntimeBridge.cpp).  Real
// Wasabi returns the skin's folder name; scripts key their per-skin
// preferences on it (getPrivateInt(getSkinName(), ...)).
extern "C" const wchar_t *wq_skin_name();

extern "C" scriptVar wq_getSkinName(maki_cmd *, int, ScriptObject *) {
    const wchar_t *n = wq_skin_name();
    return makeString(n && *n ? n : L"qtWasabi");
}

// SApplication
//
// Win9x-era skins compose their titlebar caption from the Application
// object ("Winamp " + GetVersionNumberString()); unbound these fell
// into the NULL-CALLM no-op and the caption stayed empty.  The values
// identify the skin platform being emulated (the classic 5.666
// surface), not the embedder — shipped skins feature-gate on them,
// e.g. Winamp2000SP4 deliberately degrades itself when it sees "5.8".
extern "C" scriptVar wq_appGetName(maki_cmd *, int, ScriptObject *) {
    return makeString(L"Winamp");
}
extern "C" scriptVar wq_appGetVersionString(maki_cmd *, int,
                                            ScriptObject *) {
    return makeString(L"Winamp 5.666");
}
extern "C" scriptVar wq_appGetVersionNumberString(maki_cmd *, int,
                                                  ScriptObject *) {
    return makeString(L"5.666");
}
extern "C" scriptVar wq_appGetBuildNumber(maki_cmd *, int,
                                          ScriptObject *) {
    return makeInt(3516);
}

// getDate() returns a date HANDLE (the time_t) that the getDate* extractors
// consume; getTimeOfDay() is ms since midnight.  Were constant 0 (1970
// epoch / dead time-delta).  time_t fits in int until 2038.
extern "C" scriptVar wq_getDate(maki_cmd *, int, ScriptObject *) {
    return makeInt(static_cast<int>(std::time(nullptr)));
}
extern "C" scriptVar wq_getTimeOfDay(maki_cmd *, int, ScriptObject *) {
    const std::time_t t = std::time(nullptr);
    const std::tm *lt = std::localtime(&t);
    if (!lt) return makeInt(0);
    return makeInt(((lt->tm_hour * 60 + lt->tm_min) * 60 + lt->tm_sec) * 1000);
}
// Date component extractors — take the getDate() handle (time_t).  Were
// unbound → 0 (1970 epoch).  General; latent for current skins, real for
// clock/date skins.
static const std::tm *dateTm(const scriptVar &h) {
    static thread_local std::tm cache;
    std::time_t t = static_cast<std::time_t>(vargint(h));
    if (t == 0) t = std::time(nullptr);
    const std::tm *lt = std::localtime(&t);
    if (!lt) return nullptr;
    cache = *lt;
    return &cache;
}
extern "C" scriptVar wq_getDateYear (maki_cmd*,int,ScriptObject*,scriptVar h){ auto*t=dateTm(h); return makeInt(t?t->tm_year+1900:0); }
extern "C" scriptVar wq_getDateMonth(maki_cmd*,int,ScriptObject*,scriptVar h){ auto*t=dateTm(h); return makeInt(t?t->tm_mon+1:0); }
extern "C" scriptVar wq_getDateDay  (maki_cmd*,int,ScriptObject*,scriptVar h){ auto*t=dateTm(h); return makeInt(t?t->tm_mday:0); }
extern "C" scriptVar wq_getDateDow  (maki_cmd*,int,ScriptObject*,scriptVar h){ auto*t=dateTm(h); return makeInt(t?t->tm_wday:0); }
extern "C" scriptVar wq_getDateDoy  (maki_cmd*,int,ScriptObject*,scriptVar h){ auto*t=dateTm(h); return makeInt(t?t->tm_yday:0); }
extern "C" scriptVar wq_getDateDst  (maki_cmd*,int,ScriptObject*,scriptVar h){ auto*t=dateTm(h); return makeInt(t?(t->tm_isdst>0?1:0):0); }
extern "C" scriptVar wq_getDateHour (maki_cmd*,int,ScriptObject*,scriptVar h){ auto*t=dateTm(h); return makeInt(t?t->tm_hour:0); }
extern "C" scriptVar wq_getDateMin  (maki_cmd*,int,ScriptObject*,scriptVar h){ auto*t=dateTm(h); return makeInt(t?t->tm_min:0); }
extern "C" scriptVar wq_getDateSec  (maki_cmd*,int,ScriptObject*,scriptVar h){ auto*t=dateTm(h); return makeInt(t?t->tm_sec:0); }

extern "C" scriptVar wq_isTransparencyAvailable(maki_cmd *, int, ScriptObject *) {
    return makeBoolean(1);
}

// Forward-declared in maki-bridge.h; bodies live in
// wasabi-port-link-stubs.cpp.  Declared here as plain forward so we
// don't pull the full maki-bridge.h (which exposes Qt-incompatible
// types via its Qt-side accessors).
namespace qtWasabi::Maki {
    const wchar_t *currentScriptParam();
    void setCurrentScriptId(int);
    int  currentScriptId();
    void setPlayItemMetaResolver(
        std::function<std::wstring(const std::wstring &)> resolver);
    int  fireZeroArgEventOnObject(void *recv, const wchar_t *eventName);
    int  fireOnTextChangedOnObject(void *recv, const wchar_t *newText);
}

extern "C" scriptVar wq_getParam(maki_cmd *, int /*vsd*/, ScriptObject *) {
    // Per-script `<script param="…">` — looked up by the currently-
    // dispatching script id.  SkinRuntime sets it via setCurrentScriptId
    // before each fireEventByName / fireOnSetXuiParam call.
    //
    // Real Winamp's mc.exe build pipeline substitutes `@FOO@` template
    // literals in the XML at skin-package time (using `wmgr.h`-style
    // defines).  Pre-built skins (Bento etc.) end up with literal
    // `@HAVE_LIBRARY@` in `<script param="…">` only if the build
    // pipeline didn't process the file — but the script bytecode
    // expects the resolved value, not the literal placeholder.
    //
    // The canonical resolutions for the Wasabi feature flags below
    // are: HAVE_LIBRARY = "1" (we host the ML widget), HAVE_VIDEO =
    // "1" (we support video playback through QMediaPlayer), HAVE_AVS
    // = "0" (no AVS plugin host), HAVE_BROWSER = "0" (no embedded
    // Gecko/IE).  Skins use `getParam()` + `stringToInteger()` to
    // gate tab visibility / feature switches; returning the correct
    // resolutions makes `tabcontrol.m` end up with ML/Playlist/Vis
    // visible instead of hiding everything.
    const wchar_t *raw = qtWasabi::Maki::currentScriptParam();
    if (!raw) return makeString(L"");
    // The token can appear standalone (param="@HAVE_LIBRARY@") OR embedded
    // in a comma-list (fileinfo: param="@HAVE_LIBRARY@,6", where the script
    // does getToken(getParam(),",",0)).  An exact whole-string match missed
    // the comma-list form, so `stringToInteger("@HAVE_LIBRARY@")`→0 took the
    // non-library branch (Title/Artist/Album/Track instead of …/Year/Rating).
    // Replace EVERY occurrence as a substring.  General: any script param.
    std::wstring s(raw);
    auto sub = [&s](const wchar_t *tok, const wchar_t *val) {
        const std::wstring t(tok), v(val);
        size_t pos = 0;
        while ((pos = s.find(t, pos)) != std::wstring::npos) {
            s.replace(pos, t.size(), v);
            pos += v.size();
        }
    };
    sub(L"@HAVE_LIBRARY@", L"1");   // we host the ML widget
    sub(L"@HAVE_VIDEO@",   L"1");   // QMediaPlayer video path
    sub(L"@HAVE_AVS@",     L"0");   // no AVS plugin host
    sub(L"@HAVE_BROWSER@", L"0");   // no embedded browser
    // `s` is a local std::wstring — its buffer dies when this function
    // returns, but makeString stores the bare pointer (no copy).  Intern
    // so the string outlives the call; otherwise scriptVar.sdata dangles
    // and any later consumer (stringToInteger(getParam()), getToken(...))
    // reads freed memory → garbage → 0.  Use-after-free, nondeterministic.
    return makeString(intern(s));
}

extern "C" scriptVar wq_hasVideoSupport(maki_cmd *, int, ScriptObject *) {
    // qtamp routes video frames through QMediaPlayer → QVideoSink
    // (Bento's `<windowholder hold="guid:{F0816D7B-…}">` slot would
    // host the video output if a video file was loaded).  Return
    // true so `tabcontrol.m` keeps the Video tab visible.  Reference
    // Bento (real Winamp) also returns true here under canonical
    // Windows DirectShow availability.
    return makeBoolean(1);
}

// Forward-declare the desktop-geometry bridge accessors (defined in
// SkinRuntimeBridge.cpp, re-declared in the bridge block below) so the
// viewport getters here — which appear above that block — can call them.
extern "C" {
    int wq_screen_avail_w();
    int wq_screen_avail_h();
    int wq_screen_avail_x();
    int wq_screen_avail_y();
}

extern "C" scriptVar wq_getScale(maki_cmd *, int, ScriptObject *) {
    // GuiObject.getScale() returns the layout's current scale
    // factor.  Real Winamp uses this for letterboxed / pixel-doubled
    // layouts.  Default 1.0 is correct for unscaled windows.
    //
    // Bento's `simplemaximize.m` and `maximize.m` divide the
    // viewport dimensions by this value to derive the layout's
    // natural pixel size — when the binding returned 0 (the
    // missing-method default) they crashed with "Division by zero"
    // mid-init, leaving the maximize / restore buttons untouched
    // and the chrome titlebar in a half-configured state.
    return makeDouble(1.0);
}

// System.getViewPort*FromGuiObject(o) → the work-area of the screen
// containing the object.  Maki maximize scripts compare this against the
// layout's current size to choose the maximize vs restore button, and
// resize the layout to it on maximize.  Previously hardcoded to the
// layout's native 800×600 — which made the script believe the window was
// ALWAYS maximized.  Now routed to the real desktop work area, so
// maximize/restore work on ANY skin and ANY display size.  Fall back to
// 800/600 only when there is no screen (offscreen edge case).
extern "C" scriptVar wq_getViewPortWidthFromGuiObject(maki_cmd *, int,
                                                       ScriptObject *,
                                                       scriptVar /*obj*/) {
    const int w = wq_screen_avail_w();
    return makeInt(w > 0 ? w : 800);
}

extern "C" scriptVar wq_getViewPortHeightFromGuiObject(maki_cmd *, int,
                                                        ScriptObject *,
                                                        scriptVar /*obj*/) {
    const int h = wq_screen_avail_h();
    return makeInt(h > 0 ? h : 600);
}

extern "C" scriptVar wq_getViewPortLeftFromGuiObject(maki_cmd *, int,
                                                      ScriptObject *,
                                                      scriptVar /*obj*/) {
    return makeInt(wq_screen_avail_x());
}

extern "C" scriptVar wq_getViewPortTopFromGuiObject(maki_cmd *, int,
                                                     ScriptObject *,
                                                     scriptVar /*obj*/) {
    return makeInt(wq_screen_avail_y());
}

// Zero-arg variants — System.getViewportWidth()/Height()/Left()/Top().
extern "C" scriptVar wq_getViewportWidth(maki_cmd *, int, ScriptObject *) {
    const int w = wq_screen_avail_w();
    return makeInt(w > 0 ? w : 800);
}
extern "C" scriptVar wq_getViewportHeight(maki_cmd *, int, ScriptObject *) {
    const int h = wq_screen_avail_h();
    return makeInt(h > 0 ? h : 600);
}
extern "C" scriptVar wq_getViewportLeft(maki_cmd *, int, ScriptObject *) {
    return makeInt(wq_screen_avail_x());
}
extern "C" scriptVar wq_getViewportTop(maki_cmd *, int, ScriptObject *) {
    return makeInt(wq_screen_avail_y());
}

// System.showWindow(guidOrId, preferredContainer, transient) /
// hideNamedWindow(guidOrId) / isNamedWindowVisible(guidOrId) — window
// control by GUID or container id, routed to the embedder's subwindow
// machinery (Winamp Modern's drawer scripts detach the vis/video
// drawers into their own windows through these).
extern "C" int wq_named_window(const wchar_t *ref, int op);
extern "C" scriptVar wq_showWindow(maki_cmd *, int, ScriptObject *,
                                   scriptVar guid, scriptVar /*container*/,
                                   scriptVar /*transient*/) {
    return makeInt(wq_named_window(vargstr(guid).c_str(), 1));
}
extern "C" scriptVar wq_hideNamedWindow(maki_cmd *, int, ScriptObject *,
                                        scriptVar guid) {
    return makeInt(wq_named_window(vargstr(guid).c_str(), 0));
}
extern "C" scriptVar wq_isNamedWindowVisible(maki_cmd *, int, ScriptObject *,
                                             scriptVar guid) {
    return makeInt(wq_named_window(vargstr(guid).c_str(), 2));
}

extern "C" scriptVar wq_getToken(maki_cmd *, int, ScriptObject *,
                                  scriptVar src, scriptVar sep, scriptVar n) {
    const std::wstring s = vargstr(src);
    const std::wstring d = vargstr(sep);
    int idx = (n.type == SCRIPT_INT) ? n.data.idata : 0;
    if (std::getenv("WASABIQT_TRACE_MAKI")) {
        char sb[64]={0}, db[8]={0};
        wq_wide_to_ascii(s.c_str(), sb, sizeof(sb));
        wq_wide_to_ascii(d.c_str(), db, sizeof(db));
        fprintf(stderr, "[maki] getToken('%s', '%s', %d)\n", sb, db, idx);
    }
    if (s.empty() || d.empty()) return makeString(L"");
    size_t start = 0;
    int i = 0;
    while (start <= s.size()) {
        size_t end = s.find(d[0], start);
        if (end == std::wstring::npos) end = s.size();
        if (i == idx) {
            const wchar_t *r = intern(s.substr(start, end - start));
            if (std::getenv("WASABIQT_TRACE_MAKI")) {
                char nb[64]={0};
                wq_wide_to_ascii(r, nb, sizeof(nb));
                fprintf(stderr, "[maki] getToken result='%s'\n", nb);
            }
            return makeString(r);
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

// String.strsearch(str, substr) — index of substr in str, or -1.
// Was unbound (defaulted to a 0 return), so `strsearch(s,"/") != -1`
// was always TRUE — fileinfo.maki then ran its "1/9 -> 1 of 9" split
// + translate() on plain track numbers, yielding a garbage glyph.
extern "C" scriptVar wq_strsearch(maki_cmd *, int, ScriptObject *,
                                   scriptVar str, scriptVar sub) {
    const std::wstring s = vargstr(str);
    const std::wstring n = vargstr(sub);
    if (n.empty()) return makeInt(-1);
    const size_t p = s.find(n);
    return makeInt(p == std::wstring::npos ? -1 : static_cast<int>(p));
}

// String.translate(str) — localized string.  We ship no translation
// tables, so return the source string unchanged (the en-US default)
// rather than the link-stub's garbage.
extern "C" scriptVar wq_translate(maki_cmd *, int, ScriptObject *,
                                   scriptVar str) {
    return makeString(intern(vargstr(str)));
}

// ── Wasabi String builtins (were UNBOUND → SCRIPT_INT 0, which coerces to
// the literal "0" and poisons every string slot + leaks `s != ""` guards
// across Bento/Big Bento/WinampModernPP — the project's #1 defect class).
// Pure functions; intern() the result so the SCRIPT_STRING sdata outlives
// the call (makeString stores the bare pointer — same hazard as getParam).
// Mirror the Maki String object semantics.
extern "C" scriptVar wq_strlen(maki_cmd *, int, ScriptObject *, scriptVar s) {
    return makeInt(static_cast<int>(vargstr(s).size()));
}
extern "C" scriptVar wq_strleft(maki_cmd *, int, ScriptObject *, scriptVar s, scriptVar n) {
    const std::wstring str = vargstr(s);
    int k = vargint(n); if (k < 0) k = 0; if (static_cast<size_t>(k) > str.size()) k = str.size();
    return makeString(intern(str.substr(0, k)));
}
extern "C" scriptVar wq_strright(maki_cmd *, int, ScriptObject *, scriptVar s, scriptVar n) {
    const std::wstring str = vargstr(s);
    int k = vargint(n); if (k < 0) k = 0; if (static_cast<size_t>(k) > str.size()) k = str.size();
    return makeString(intern(str.substr(str.size() - k)));
}
// strmid(str, from [, len]) — len omitted = to end.  Clamp both ends.
extern "C" scriptVar wq_strmid(maki_cmd *, int, ScriptObject *, scriptVar s, scriptVar from, scriptVar len) {
    const std::wstring str = vargstr(s);
    int f = vargint(from); if (f < 0) f = 0;
    if (static_cast<size_t>(f) >= str.size()) return makeString(L"");
    int l = vargint(len); if (l < 0) l = 0;
    return makeString(intern(str.substr(f, static_cast<size_t>(l))));
}
extern "C" scriptVar wq_strlower(maki_cmd *, int, ScriptObject *, scriptVar s) {
    std::wstring str = vargstr(s);
    for (auto &c : str) c = towlower(c);
    return makeString(intern(str));
}
extern "C" scriptVar wq_strupper(maki_cmd *, int, ScriptObject *, scriptVar s) {
    std::wstring str = vargstr(s);
    for (auto &c : str) c = towupper(c);
    return makeString(intern(str));
}

// integerToTime(ms) → "M:SS"; integerToLongTime(ms) → "H:MM:SS".
// Both take MILLISECONDS (Wasabi divides by 60000/1000) — every Maki
// producer feeding them (getPosition, getPlayItemLength, the "length"
// metadata field, getTimeOfDay) is on the millisecond scale too.
extern "C" scriptVar wq_integerToTime(maki_cmd *, int, ScriptObject *, scriptVar v) {
    int ms = vargint(v); if (ms < 0) ms = 0;
    wchar_t buf[32];
    swprintf(buf, 32, L"%d:%02d", ms / 60000, ms % 60000 / 1000);
    return makeString(intern(std::wstring(buf)));
}
extern "C" scriptVar wq_integerToLongTime(maki_cmd *, int, ScriptObject *, scriptVar v) {
    int ms = vargint(v); if (ms < 0) ms = 0;
    wchar_t buf[40];
    swprintf(buf, 40, L"%d:%02d:%02d",
             ms / 3600000, (ms % 3600000) / 60000, ms % 60000 / 1000);
    return makeString(intern(std::wstring(buf)));
}

// floatToString(value, ndigits) → string; stringToFloat(str) → float.
// Were UNBOUND → int 0 (wrong type + value); break EQ dB / parsed floats.
extern "C" scriptVar wq_floatToString(maki_cmd *, int, ScriptObject *, scriptVar f, scriptVar prec) {
    int p = vargint(prec); if (p < 0) p = 0; if (p > 12) p = 12;
    wchar_t buf[64]; swprintf(buf, 64, L"%.*f", p, vargdouble(f));
    return makeString(intern(std::wstring(buf)));
}
extern "C" scriptVar wq_stringToFloat(maki_cmd *, int, ScriptObject *, scriptVar s) {
    return makeFloat(static_cast<float>(vargdouble(s)));
}

// random(n) — was UNBOUND → 0 (every cycle/shuffle pinned to index 0).
extern "C" scriptVar wq_random(maki_cmd *, int, ScriptObject *, scriptVar n) {
    const int m = vargint(n);
    if (m <= 0) return makeInt(0);
    return makeInt(std::rand() % m);
}

// ── Math family — ALL were unbound → int 0.  Not used by Bento/Big
// Bento, but MANDATORY for procedural/animated/visualiser skins that
// compute positions trigonometrically.  Pure functions, return
// SCRIPT_DOUBLE (integer→INT, frac→DOUBLE), matching the Maki semantics.
extern "C" scriptVar wq_sin (maki_cmd *,int,ScriptObject *,scriptVar d){ return makeDouble(std::sin (vargdouble(d))); }
extern "C" scriptVar wq_cos (maki_cmd *,int,ScriptObject *,scriptVar d){ return makeDouble(std::cos (vargdouble(d))); }
extern "C" scriptVar wq_tan (maki_cmd *,int,ScriptObject *,scriptVar d){ return makeDouble(std::tan (vargdouble(d))); }
extern "C" scriptVar wq_asin(maki_cmd *,int,ScriptObject *,scriptVar d){ return makeDouble(std::asin(vargdouble(d))); }
extern "C" scriptVar wq_acos(maki_cmd *,int,ScriptObject *,scriptVar d){ return makeDouble(std::acos(vargdouble(d))); }
extern "C" scriptVar wq_atan(maki_cmd *,int,ScriptObject *,scriptVar d){ return makeDouble(std::atan(vargdouble(d))); }
extern "C" scriptVar wq_atan2(maki_cmd *,int,ScriptObject *,scriptVar y,scriptVar x){ return makeDouble(std::atan2(vargdouble(y), vargdouble(x))); }
extern "C" scriptVar wq_pow (maki_cmd *,int,ScriptObject *,scriptVar x,scriptVar y){ return makeDouble(std::pow(vargdouble(x), vargdouble(y))); }
extern "C" scriptVar wq_sqr (maki_cmd *,int,ScriptObject *,scriptVar d){ const double v=vargdouble(d); return makeDouble(v*v); }
extern "C" scriptVar wq_sqrt(maki_cmd *,int,ScriptObject *,scriptVar d){ const double v=vargdouble(d); return makeDouble(v>=0.0?std::sqrt(v):0.0); }
extern "C" scriptVar wq_ln  (maki_cmd *,int,ScriptObject *,scriptVar d){ const double v=vargdouble(d); return makeDouble(v>0.0?std::log(v):0.0); }
extern "C" scriptVar wq_log10(maki_cmd *,int,ScriptObject *,scriptVar d){ const double v=vargdouble(d); return makeDouble(v>0.0?std::log10(v):0.0); }
extern "C" scriptVar wq_integer(maki_cmd *,int,ScriptObject *,scriptVar d){ return makeInt(static_cast<int>(vargdouble(d))); }
extern "C" scriptVar wq_frac(maki_cmd *,int,ScriptObject *,scriptVar d){ const double v=vargdouble(d); return makeDouble(v - static_cast<double>(static_cast<long long>(v))); }

extern "C" scriptVar wq_integerToString(maki_cmd *, int, ScriptObject *,
                                         scriptVar i) {
    // VCPU arithmetic (OPCODE_SUB / MUL / DIV) stores the result in
    // `ddata` and sets type = SCRIPT_DOUBLE.  Reading idata for those
    // returns the low 32 bits of the IEEE-754 representation, which
    // is almost always 0 — that's why our Maki handlers used to
    // setXmlParam("x", "0").  Read the correct field per type.
    int v = 0;
    switch (i.type) {
        case SCRIPT_INT:
        case SCRIPT_BOOLEAN: v = i.data.idata; break;
        case SCRIPT_FLOAT:   v = static_cast<int>(i.data.fdata); break;
        case SCRIPT_DOUBLE:  v = static_cast<int>(i.data.ddata); break;
        default: return makeString(L"0");
    }
    wchar_t buf[32];
    std::swprintf(buf, 32, L"%d", v);
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

// ── Preference persistence ──────────────────────────────────────
// The Wasabi API persists private/public ints+strings and the Config
// attributes to studio.xnf; ours go through one bridge-backed INI
// (SkinRuntimeBridge.cpp wq_pref_*).  A persisted value always wins
// over the in-process seed/default; every write is stored.
extern "C" {
int  wq_pref_load(const wchar_t *scope, const wchar_t *name,
                  wchar_t *out, int cap);
void wq_pref_save(const wchar_t *scope, const wchar_t *name,
                  const wchar_t *value);
}
static bool prefLoad(const wchar_t *scope, const std::wstring &name,
                     std::wstring &out) {
    wchar_t buf[2048];
    const int n = wq_pref_load(scope, name.c_str(), buf, 2048);
    if (n < 0) return false;
    out.assign(buf, size_t(n));
    return true;
}
static void prefSave(const wchar_t *scope, const std::wstring &name,
                     const std::wstring &value) {
    wq_pref_save(scope, name.c_str(), value.c_str());
}

// In-memory (section, key) → int cache over the persisted store.  The
// seeded entries are qtamp's SHIPPED first-run defaults (drawer open,
// EQ tab selected); a persisted user value overrides them.
// configtabs.maki reads back DrawerOpen and ConfigTab in onScriptLoaded
// to decide whether to call OpenDrawer; pre-seeding DrawerOpen=1 lets
// the script open the drawer via real Maki dispatch (drawer y=-147,
// button.open.hide, …) instead of needing static fallbacks.
static std::unordered_map<std::wstring, int> &privateIntStore() {
    static std::unordered_map<std::wstring, int> m{
        // Default qtamp preferences mirror what we used to force
        // statically in runKnownScripts: drawer is open by default,
        // EQ tab is selected.  Override at runtime by setting these
        // before SkinRuntime::dispatchOnScriptLoaded fires.
        { L"winamp5|DrawerOpen", 1 },
        { L"winamp5|ConfigTab",  1 },
    };
    return m;
}

// Shared cores — also reachable from the embedder side (qtamp's
// Preferences writes the same TimerElapsedRemaining slot the skin
// scripts read) through the wq_privateInt* C surface below.
static int privateIntGet(const std::wstring &sec, const std::wstring &key,
                         int def) {
    std::wstring k = sec + L"|" + key;
    auto &m = privateIntStore();
    std::wstring stored;
    if (prefLoad(L"private-int", k, stored)) {
        const int v = int(std::wcstol(stored.c_str(), nullptr, 10));
        m[k] = v;
        return v;
    }
    auto it = m.find(k);
    if (it != m.end()) return it->second;
    return def;
}
static void privateIntSet(const std::wstring &sec, const std::wstring &key,
                          int v) {
    std::wstring k = sec + L"|" + key;
    privateIntStore()[k] = v;
    prefSave(L"private-int", k, std::to_wstring(v));
}
extern "C" int wq_privateIntGet(const wchar_t *sec, const wchar_t *key,
                                int def) {
    if (!sec || !key) return def;
    return privateIntGet(sec, key, def);
}
extern "C" void wq_privateIntSet(const wchar_t *sec, const wchar_t *key,
                                 int v) {
    if (!sec || !key) return;
    privateIntSet(sec, key, v);
}

extern "C" scriptVar wq_getPrivateInt(maki_cmd *, int, ScriptObject *,
                                       scriptVar sec, scriptVar key,
                                       scriptVar def) {
    if (sec.type != SCRIPT_STRING || !sec.data.sdata ||
        key.type != SCRIPT_STRING || !key.data.sdata)
        return def;
    int d = 0;
    switch (def.type) {
        case SCRIPT_INT:
        case SCRIPT_BOOLEAN: d = def.data.idata; break;
        case SCRIPT_FLOAT:   d = static_cast<int>(def.data.fdata); break;
        case SCRIPT_DOUBLE:  d = static_cast<int>(def.data.ddata); break;
        default: d = 0;
    }
    return makeInt(privateIntGet(sec.data.sdata, key.data.sdata, d));
}

extern "C" scriptVar wq_setPrivateInt(maki_cmd *, int, ScriptObject *,
                                       scriptVar sec, scriptVar key,
                                       scriptVar val) {
    if (sec.type != SCRIPT_STRING || !sec.data.sdata ||
        key.type != SCRIPT_STRING || !key.data.sdata)
        return makeVoid();
    int v = 0;
    switch (val.type) {
        case SCRIPT_INT:
        case SCRIPT_BOOLEAN: v = val.data.idata; break;
        case SCRIPT_FLOAT:   v = static_cast<int>(val.data.fdata); break;
        case SCRIPT_DOUBLE:  v = static_cast<int>(val.data.ddata); break;
        default: v = 0;
    }
    privateIntSet(sec.data.sdata, key.data.sdata, v);
    return makeVoid();
}

// String persistence store (mirrors privateIntStore).  getPrivate/PublicString
// were UNBOUND → int 0 → the literal "0", so PlaylistPro's search-history
// string (getPublicString 'cPro.PlaylistPro.history') was the constant "0"
// and `history != ""` guards leaked.  Returns the stored value or the
// caller's default; intern() so the SCRIPT_STRING outlives the call.
static std::unordered_map<std::wstring, std::wstring> &stringStore() {
    static std::unordered_map<std::wstring, std::wstring> m;
    return m;
}
extern "C" scriptVar wq_getPrivateString(maki_cmd *, int, ScriptObject *,
                                          scriptVar sec, scriptVar key, scriptVar def) {
    if (sec.type != SCRIPT_STRING || !sec.data.sdata ||
        key.type != SCRIPT_STRING || !key.data.sdata) return def;
    const std::wstring k = std::wstring(sec.data.sdata) + L"|" + std::wstring(key.data.sdata);
    std::wstring stored;
    if (prefLoad(L"private-str", k, stored)) {
        stringStore()[k] = stored;
        return makeString(intern(stored));
    }
    auto it = stringStore().find(k);
    return it != stringStore().end() ? makeString(intern(it->second)) : def;
}
extern "C" scriptVar wq_setPrivateString(maki_cmd *, int, ScriptObject *,
                                          scriptVar sec, scriptVar key, scriptVar val) {
    if (sec.type != SCRIPT_STRING || !sec.data.sdata ||
        key.type != SCRIPT_STRING || !key.data.sdata) return makeVoid();
    const std::wstring k = std::wstring(sec.data.sdata) + L"|" + std::wstring(key.data.sdata);
    stringStore()[k] = vargstr(val);
    prefSave(L"private-str", k, stringStore()[k]);
    return makeVoid();
}
// PublicString is keyed by a single item string (2 args).
extern "C" scriptVar wq_getPublicString(maki_cmd *, int, ScriptObject *,
                                         scriptVar item, scriptVar def) {
    if (item.type != SCRIPT_STRING || !item.data.sdata) return def;
    const std::wstring k = std::wstring(L"pub|") + item.data.sdata;
    std::wstring stored;
    if (prefLoad(L"public-str", k, stored)) {
        stringStore()[k] = stored;
        return makeString(intern(stored));
    }
    auto it = stringStore().find(k);
    return it != stringStore().end() ? makeString(intern(it->second)) : def;
}
extern "C" scriptVar wq_setPublicString(maki_cmd *, int, ScriptObject *,
                                         scriptVar item, scriptVar val) {
    if (item.type != SCRIPT_STRING || !item.data.sdata) return makeVoid();
    const std::wstring k = std::wstring(L"pub|") + item.data.sdata;
    stringStore()[k] = vargstr(val);
    prefSave(L"public-str", k, stringStore()[k]);
    return makeVoid();
}

// Wasabi PublicInt is keyed by a SINGLE item string: getPublicInt(item, def)
// / setPublicInt(item, value) — 2 args, not 3.  The old 3-arg registration
// popped an extra operand-stack slot (VSP imbalance) and read a slot the
// call never pushed.  Match the real arity.
extern "C" scriptVar wq_getPublicInt(maki_cmd *, int, ScriptObject *,
                                      scriptVar item, scriptVar def) {
    if (item.type != SCRIPT_STRING || !item.data.sdata) return def;
    std::wstring stored;
    if (prefLoad(L"public-int", item.data.sdata, stored))
        return makeInt(int(std::wcstol(stored.c_str(), nullptr, 10)));
    return def;
}

extern "C" scriptVar wq_setPublicInt(maki_cmd *, int, ScriptObject *,
                                      scriptVar item, scriptVar val) {
    if (item.type != SCRIPT_STRING || !item.data.sdata) return makeVoid();
    int v = 0;
    switch (val.type) {
        case SCRIPT_INT:
        case SCRIPT_BOOLEAN: v = val.data.idata; break;
        case SCRIPT_FLOAT:   v = int(val.data.fdata); break;
        case SCRIPT_DOUBLE:  v = int(val.data.ddata); break;
        default: break;
    }
    prefSave(L"public-int", item.data.sdata, std::to_wstring(v));
    return makeVoid();
}

// Qt-side bridge accessors — see src/SkinRuntimeBridge.cpp.
// Forward-declared up here so wq_getScriptGroup can use them.
extern "C" {
    void *wq_widget_findById(const wchar_t *id);
    void *wq_widget_findByIdScoped(void *parentHandle, const wchar_t *id);
    void *wq_widget_parent(void *handle);
    void  wq_widget_setFrameDivider(void *handle, int pos);
    void  wq_timer_arm(void *timerSO, int ms);
    void  wq_timer_setDelay(void *timerSO, int ms);
    void  wq_timer_stop(void *timerSO);
    int   wq_timer_isRunning(void *timerSO);
    void  wq_timer_kill(void *timerSO);
    void  wq_widget_setAttr(void *handle, const wchar_t *name, const wchar_t *value);
    const wchar_t *wq_widget_getAttr(void *handle, const wchar_t *name);
    int   wq_widget_getAttrInt(void *handle, const wchar_t *name);
    int   wq_widget_resolvedWidth(void *handle);
    int   wq_widget_resolvedHeight(void *handle);
    bool  wq_widget_hasResolvedRect(void *handle);
    int   wq_widget_resolvedX(void *handle);
    int   wq_widget_resolvedY(void *handle);
    int   wq_widget_childCount(void *handle);
    void *wq_widget_childAt(void *handle, int idx);
    int   wq_widget_textWidth(void *handle);
    int   wq_widget_bitmapWidth(void *handle);
    int   wq_widget_bitmapHeight(void *handle);
    void *wq_layout_root();
    void *wq_script_owner(int sid);
    int   wq_playback_status();
    void  wq_layout_set_target_w(int w);
    void  wq_layout_set_target_h(int h);
    void  wq_layout_set_target_x(int x);
    void  wq_layout_set_target_y(int y);
    void  wq_layout_goto_target();
    int   wq_screen_avail_w();
    int   wq_screen_avail_h();
    int   wq_screen_avail_x();
    int   wq_screen_avail_y();
    void  wq_widget_animate_target(void *handle, int tx, int ty,
                                   int tw, int th, int durationMs);
    int   wq_widget_click_action(void *handle, int right);
    void  wq_eq_set_band(int band, int val);
    int   wq_eq_get_band(int band);
    void  wq_slider_set_position(void *handle, int value255);
    int   wq_slider_get_position(void *handle);
    void  wq_volume_set(int v255);
    int   wq_volume_get();
}

extern "C" scriptVar wq_getStatus(maki_cmd *, int, ScriptObject *) {
    return makeInt(wq_playback_status());
}

// Per-receiver setTarget{X,Y,W,H} + gotoTarget.  Maki scripts call
// these on either the layout root (Layout) — to resize the host
// window — or on individual widgets (Group/Layer) to animate their
// position/size.  We can't tell the receiver class from a void*
// ScriptObject, so we stash per-receiver state in a small map and
// dispatch on gotoTarget: if the receiver is the layout root, fire
// the host resize; otherwise mutate the widget's own x/y/w/h attrs.
struct TargetState { int x = -1, y = -1, w = -1, h = -1; };
static std::unordered_map<void *, TargetState> &targetMap() {
    static std::unordered_map<void *, TargetState> m;
    return m;
}

// Coerce any numeric scriptVar to int, picking the right union field
// based on its dynamic type.  Mirrors SOM::makeDouble's contract but
// drops to int.  Returns 0 for non-numeric types.
static inline int makeIntFromVar(const scriptVar &v) {
    switch (v.type) {
        case SCRIPT_INT:
        case SCRIPT_BOOLEAN: return v.data.idata;
        case SCRIPT_FLOAT:   return static_cast<int>(v.data.fdata);
        case SCRIPT_DOUBLE:  return static_cast<int>(v.data.ddata);
        default:             return 0;
    }
}

extern "C" scriptVar wq_setTargetW(maki_cmd *, int, ScriptObject *o,
                                     scriptVar v) {
    targetMap()[(void *)o].w = makeIntFromVar(v);
    return makeVoid();
}
extern "C" scriptVar wq_setTargetH(maki_cmd *, int, ScriptObject *o,
                                     scriptVar v) {
    targetMap()[(void *)o].h = makeIntFromVar(v);
    return makeVoid();
}
extern "C" scriptVar wq_setTargetX(maki_cmd *, int, ScriptObject *o,
                                     scriptVar v) {
    targetMap()[(void *)o].x = makeIntFromVar(v);
    return makeVoid();
}
extern "C" scriptVar wq_setTargetY(maki_cmd *, int, ScriptObject *o,
                                     scriptVar v) {
    targetMap()[(void *)o].y = makeIntFromVar(v);
    return makeVoid();
}
extern "C" scriptVar wq_setTargetSpeed(maki_cmd *, int, ScriptObject *,
                                         scriptVar) {
    return makeVoid();  // We don't animate; speed is ignored.
}
extern "C" scriptVar wq_gotoTarget(maki_cmd *, int, ScriptObject *o) {
    auto &m = targetMap();
    auto it = m.find((void *)o);
    if (it == m.end()) return makeVoid();
    const TargetState t = it->second;
    m.erase(it);
    // Layout root → host resize callback.
    if (o == static_cast<ScriptObject *>(wq_layout_root())) {
        if (t.w >= 0) wq_layout_set_target_w(t.w);
        if (t.h >= 0) wq_layout_set_target_h(t.h);
        if (t.x >= 0) wq_layout_set_target_x(t.x);
        if (t.y >= 0) wq_layout_set_target_y(t.y);
        wq_layout_goto_target();
        return makeVoid();
    }
    // Widget — drive the x/y/w/h attrs through a QVariantAnimation so
    // the drawer slide (configtabs.m → drawer.setTargetY(-147) /
    // gotoTarget) eases in/out instead of snapping.  The Qt-side
    // helper fires `onTargetReached` once the tween completes.
    // 350 ms reads as a deliberate slide rather than a snap, matches
    // the cadence of WinampModernPP's drawer.m when it animates on
    // real Winamp at default setTargetSpeed(1).
    if (::getenv("WASABIQT_TRACE_MAKI"))
        ::fprintf(stderr,
            "[maki] widget gotoTarget tx=%d ty=%d tw=%d th=%d\n",
            t.x, t.y, t.w, t.h);
    wq_widget_animate_target((void *)o, t.x, t.y, t.w, t.h, 350);
    return makeVoid();
}

// GuiObject.resize(x,y,w,h) — set position + size in one call.  The Maki
// semantics apply immediately and cascade a repaint.  We route through the
// SAME setTarget+gotoTarget machinery: for the layout root this fires the
// host resize (g_skinResize); for a widget it drives its x/y/w/h.  Was
// unbound → every script resize() was a silent no-op (e.g.
// simplemaximize.maki's maximize/restore did nothing).
extern "C" scriptVar wq_resize(maki_cmd *, int, ScriptObject *o,
                                scriptVar x, scriptVar y,
                                scriptVar w, scriptVar h) {
    if (!o) return makeVoid();
    auto &t = targetMap()[(void *)o];
    t.x = makeIntFromVar(x);
    t.y = makeIntFromVar(y);
    t.w = makeIntFromVar(w);
    t.h = makeIntFromVar(h);
    if (::getenv("WASABIQT_TRACE_MAKI"))
        ::fprintf(stderr, "[maki] resize x=%d y=%d w=%d h=%d\n",
                  t.x, t.y, t.w, t.h);
    return wq_gotoTarget(nullptr, 0, o);
}

// GuiObject.getEnabled() — symmetry with the bound setEnabled().  Reads
// the `enabled` attr (default ENABLED).  Was UNBOUND → int-0 (disabled),
// so every `if (o.getEnabled())` guard in a script took the wrong branch.
extern "C" scriptVar wq_getEnabled(maki_cmd *, int, ScriptObject *o) {
    if (!o) return makeBoolean(0);
    const wchar_t *v = wq_widget_getAttr(o, L"enabled");
    if (!v || !*v) return makeBoolean(1);   // absent ⇒ enabled
    return makeBoolean(v[0] != L'0');
}

// GuiObject.getGuiRelatX/Y/W/H() — the relative-sizing flags.
// Scripts branch on whether a widget scales with its parent.  Were
// unbound → int-0 so relat-conditional logic silently mis-fired.
static inline scriptVar relatFlag(ScriptObject *o, const wchar_t *attr) {
    // The Maki semantics return the relativity CODE as an int (0=fixed,
    // 1=parent-relative, 2=percent), NOT a bool — makeBoolean collapsed
    // code 2 to 1 and gave scripts a SCRIPT_BOOLEAN where arithmetic on
    // the relat mode expects an int.  Return the int code.
    if (!o) return makeInt(0);
    const wchar_t *v = wq_widget_getAttr(o, attr);
    return makeInt(v && *v ? static_cast<int>(std::wcstol(v, nullptr, 10)) : 0);
}
extern "C" scriptVar wq_getGuiRelatX(maki_cmd *, int, ScriptObject *o) {
    return relatFlag(o, L"relatx");
}
extern "C" scriptVar wq_getGuiRelatY(maki_cmd *, int, ScriptObject *o) {
    return relatFlag(o, L"relaty");
}
extern "C" scriptVar wq_getGuiRelatW(maki_cmd *, int, ScriptObject *o) {
    return relatFlag(o, L"relatw");
}
extern "C" scriptVar wq_getGuiRelatH(maki_cmd *, int, ScriptObject *o) {
    return relatFlag(o, L"relath");
}

extern "C" scriptVar wq_getScriptGroup(maki_cmd *, int, ScriptObject *o) {
    // The script's enclosing <group>/<groupdef> — recorded at parse
    // time, resolved at load time, looked up here by the currently-
    // dispatching script id.  Scripts call this to get a handle they
    // can run findObject/getObject on for sibling widgets, plus call
    // getWidth/getHeight against the group's own bounds.
    //
    // Fallbacks (in priority order):
    //   1) the parse-time owner-group widget
    //   2) the layout root (so global findObject still works — same
    //      behaviour the old hand-coded fallback provided)
    //   3) the receiver (the SystemObject — a last resort so we never
    //      hand back a null group and crash callers that don't null-check)
    const int sid = qtWasabi::Maki::currentScriptId();
    void *owner = (sid >= 0) ? wq_script_owner(sid) : nullptr;
    void *root  = wq_layout_root();
    if (std::getenv("WASABIQT_TRACE_MAKI"))
        std::fprintf(stderr,
                     "[maki] getScriptGroup sid=%d owner=%p root=%p recv=%p\n",
                     sid, owner, root, (void *)o);
    if (owner) return makeObject(static_cast<ScriptObject *>(owner));
    if (root)  return makeObject(static_cast<ScriptObject *>(root));
    return makeObject(o);
}

// ── Play-item metadata resolver ─────────────────────────────────
// The embedder (qtamp) registers this so fileinfo.maki's
// System.getPlayItem*/getPlayItemMetaDataString calls reach the host's
// real QMediaPlayer metadata.  Keys are lower-case: "playitem:string"
// (filename), "playitem:displaytitle", "decoder", "meta:<field>".
// Unset, or empty return → "" (the canonical idle "no track" value, so
// fileinfo.maki keeps the empty lines hidden).  Declared in
// maki-bridge.h for SkinRuntime to set.
static std::function<std::wstring(const std::wstring &)> g_metaResolver;
void qtWasabi::Maki::setPlayItemMetaResolver(
        std::function<std::wstring(const std::wstring &)> r) {
    g_metaResolver = std::move(r);
}
static const wchar_t *resolveMeta(const std::wstring &key) {
    const bool trace = std::getenv("WASABIQT_TRACE_META") != nullptr;
    if (!g_metaResolver) {
        if (trace) std::fprintf(stderr, "[meta] %ls -> (no resolver)\n", key.c_str());
        return L"";
    }
    const std::wstring v = g_metaResolver(key);
    if (trace) std::fprintf(stderr, "[meta] %ls -> '%ls'\n", key.c_str(), v.c_str());
    return v.empty() ? L"" : intern(v);
}

extern "C" scriptVar wq_getPlayItemMetaDataString(maki_cmd *, int,
                                                    ScriptObject *, scriptVar field) {
    return makeString(resolveMeta(L"meta:" + vargstr(field)));
}

extern "C" scriptVar wq_getPlayItemDisplayTitle(maki_cmd *, int, ScriptObject *) {
    return makeString(resolveMeta(L"playitem:displaytitle"));
}

// SystemObject string getters that fileinfo.m / songinfo.m / drawer.m
// branch on with `if (s != "")` to decide whether to .show() a track-
// info line.  When no track is loaded the canonical Wasabi behaviour
// is empty-string return → branch fails → line stays hidden.  Without
// explicit bindings these would dispatch to the safe int-0 no-op
// path, and many skins' "track playing?" branches do `s != ""` not
// `s != 0`, so a numeric 0 (which our wstring coercion shows as "0",
// not "") leaks the branch open and visibly shows e.g. "Decoder:" /
// "Stream:" / "Disc:" labels on the empty-state chrome.  Returning a
// real empty wstring is skin-agnostic: every Wasabi skin's idle state
// is "no track" and these calls all return empty there.
extern "C" scriptVar wq_getPlayItemString(maki_cmd *, int, ScriptObject *) {
    return makeString(resolveMeta(L"playitem:string"));
}
extern "C" scriptVar wq_getDecoderName(maki_cmd *, int, ScriptObject *,
                                         scriptVar) {
    return makeString(resolveMeta(L"decoder"));
}
extern "C" scriptVar wq_getFileName(maki_cmd *, int, ScriptObject *,
                                       scriptVar) {
    return makeString(resolveMeta(L"playitem:string"));
}
// Path/string helpers — were unbound → 0 → "0" for filename-derived labels
// and the demo.mp3 branding check (fileinfo's removePath(...)=="demo.mp3").
// Pure functions; intern() the result.  General.
extern "C" scriptVar wq_removePath(maki_cmd *, int, ScriptObject *, scriptVar p) {
    const std::wstring s = vargstr(p);
    const size_t i = s.find_last_of(L"/\\");
    return makeString(intern(i == std::wstring::npos ? s : s.substr(i + 1)));
}
extern "C" scriptVar wq_getExtension(maki_cmd *, int, ScriptObject *, scriptVar p) {
    const std::wstring s = vargstr(p);
    const size_t slash = s.find_last_of(L"/\\");
    const size_t dot   = s.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
        return makeString(L"");
    return makeString(intern(s.substr(dot + 1)));
}
// getExtFamily(path-or-ext) → "Audio"/"Video"/"" by extension.  The Wasabi
// API routes this through a core extension-family service; we map the
// common families directly.
extern "C" scriptVar wq_getExtFamily(maki_cmd *, int, ScriptObject *, scriptVar p) {
    std::wstring s = vargstr(p);
    const size_t dot = s.find_last_of(L'.');
    if (dot != std::wstring::npos) s = s.substr(dot + 1);
    for (auto &c : s) c = towlower(c);
    static const wchar_t *kAudio[] = { L"mp3",L"ogg",L"flac",L"wav",L"m4a",L"aac",
        L"wma",L"opus",L"ape",L"wv",L"mpc",L"aiff",L"aif",L"mp2",L"mka",nullptr };
    static const wchar_t *kVideo[] = { L"avi",L"mp4",L"mkv",L"mov",L"wmv",L"flv",
        L"webm",L"mpg",L"mpeg",L"m4v",L"3gp",L"ogv",L"ts",nullptr };
    for (const wchar_t **e = kAudio; *e; ++e) if (s == *e) return makeString(L"Audio");
    for (const wchar_t **e = kVideo; *e; ++e) if (s == *e) return makeString(L"Video");
    return makeString(L"");
}

// getSongInfoText() — the multi-line song-info blob (bitrate/khz/channels/
// …) songinfo.m strsearches for "tereo"/"ono"/"annels".  Was UNBOUND → int 0
// → "0", which made strsearch return -1 (no channel icon) AND leaked the
// `if (sit != "")` bitrate/freq guards.  Route through the meta resolver
// ("songinfo" key); empty when idle/unknown (correct neutral, not "0").
extern "C" scriptVar wq_getSongInfoText(maki_cmd *, int, ScriptObject *) {
    return makeString(resolveMeta(L"songinfo"));
}
// Parse a resolver value to int (seconds / count / index).  Empty → 0.
static int resolveMetaInt(const wchar_t *key) {
    const wchar_t *s = resolveMeta(std::wstring(key));
    return (s && *s) ? static_cast<int>(std::wcstol(s, nullptr, 10)) : 0;
}
// getPlayItemLength() → milliseconds (Wasabi returns core_getLength(0);
// scripts feed it straight into integerToTime and compare against ms
// constants); getPlaylistLength()/getPlaylistIndex().  Were UNBOUND →
// int 0 → dead seek range / total-time, zero-iteration playlist loops.
extern "C" scriptVar wq_getPlayItemLength(maki_cmd *, int, ScriptObject *) {
    return makeInt(resolveMetaInt(L"playitem:length"));
}
// System.getPosition() → playback position in milliseconds (Wasabi
// returns core_getPosition(0)).  Class-scoped: the flat `getPosition`
// name belongs to Slider/Frame receivers, so the System split lives in
// the class registry (maki-classes.cpp).
extern "C" scriptVar wq_systemGetPosition(maki_cmd *, int, ScriptObject *) {
    return makeInt(resolveMetaInt(L"playitem:position"));
}
extern "C" scriptVar wq_getPlaylistLength(maki_cmd *, int, ScriptObject *) {
    return makeInt(resolveMetaInt(L"playlist:length"));
}
extern "C" scriptVar wq_getPlaylistIndex(maki_cmd *, int, ScriptObject *) {
    return makeInt(resolveMetaInt(L"playlist:index"));
}

// Timer.setDelay(ms) / Timer.start() / Timer.stop().  Backed by real
// QTimers (wq_timer_arm/kill in SkinRuntimeBridge.cpp).
//
// Faithful Maki Timer semantics: setDelay only records the period and
// re-arms IFF the timer is already started; start() arms + sets started;
// stop() clears started (object survives); isRunning() returns the
// started flag.  This is what makes the suicore/mcvcore deferred-show
// debounce `if (callbackTimer.isRunning()) return;` behave correctly.
static std::unordered_map<void *, int> g_timerDelays;
extern "C" scriptVar wq_setDelay(maki_cmd *, int, ScriptObject *o, scriptVar ms) {
    if (!o) return makeVoid();
    const int d = (ms.type == SCRIPT_INT) ? ms.data.idata
                : (ms.type == SCRIPT_STRING && ms.data.sdata)
                    ? static_cast<int>(std::wcstol(ms.data.sdata, nullptr, 10)) : 0;
    g_timerDelays[o] = d;
    wq_timer_setDelay(o, d);
    return makeVoid();
}
extern "C" scriptVar wq_timerStart(maki_cmd *, int, ScriptObject *o) {
    if (!o) return makeVoid();
    auto it = g_timerDelays.find(o);
    wq_timer_arm(o, it != g_timerDelays.end() ? it->second : 1);
    return makeVoid();
}

extern "C" scriptVar wq_show(maki_cmd *, int, ScriptObject *o) {
    if (o) wq_widget_setAttr(o, L"visible", L"1");
    return makeVoid();
}
extern "C" scriptVar wq_hide(maki_cmd *, int, ScriptObject *o) {
    if (o) wq_widget_setAttr(o, L"visible", L"0");
    return makeVoid();
}
// GuiObject.leftClick()/rightClick() — programmatically trigger a click,
// mirroring Wasabi's Button::onLeftPush/onRightPush: fire the onLeftClick /
// onRightClick script callback first, then — only if the script didn't
// consume it — perform the widget's `action=` exactly as a real click would
// (transport/window verbs, TOGGLE, action_target).  Skins use this to
// delegate one widget's click to another (a drawer's "X" forwards to the
// open/close button, a toolbar button forwards to a transport button);
// without it those delegating buttons are dead.  General across every skin.
extern "C" scriptVar wq_leftClick(maki_cmd *, int, ScriptObject *o) {
    if (o) {
        const int fired = qtWasabi::Maki::fireZeroArgEventOnObject(
            o, L"onLeftClick");
        if (fired == 0) wq_widget_click_action(o, 0);
    }
    return makeVoid();
}
extern "C" scriptVar wq_rightClick(maki_cmd *, int, ScriptObject *o) {
    if (o) {
        const int fired = qtWasabi::Maki::fireZeroArgEventOnObject(
            o, L"onRightClick");
        if (fired == 0) wq_widget_click_action(o, 1);
    }
    return makeVoid();
}
// System.setEqBand(band, val) / getEqBand(band) — the graphic-equalizer API
// every skin's EQ uses (reset button, +/- nudges, preset recall).  `band` is
// 0-9; `val` is Wasabi's signed gain (-127..127, 0 = flat).  Routed to the
// host so it drives both the EQ sliders and the audio in lockstep.  Were
// unbound (silent no-op) → the whole scripted EQ was dead on every skin.
extern "C" scriptVar wq_setEqBand(maki_cmd *, int, ScriptObject *,
                                  scriptVar band, scriptVar val) {
    wq_eq_set_band(makeIntFromVar(band), makeIntFromVar(val));
    return makeVoid();
}
extern "C" scriptVar wq_getEqBand(maki_cmd *, int, ScriptObject *,
                                  scriptVar band) {
    return makeInt(wq_eq_get_band(makeIntFromVar(band)));
}
// System.setVolume(v)/getVolume() — the Winamp API volume is 0..255; the
// embedder maps onto its own store.  Were unbound (silent no-op) so a skin's
// volume +/- / mute buttons (System.setVolume(0/255)) did nothing.
extern "C" scriptVar wq_setVolume(maki_cmd *, int, ScriptObject *, scriptVar v) {
    wq_volume_set(makeIntFromVar(v));
    return makeVoid();
}
extern "C" scriptVar wq_getVolume(maki_cmd *, int, ScriptObject *) {
    return makeInt(wq_volume_get());
}
extern "C" scriptVar wq_stop(maki_cmd *, int, ScriptObject *o) {
    if (o) wq_timer_stop(o);   // Maki Timer.stop — clear started, keep the object
    return makeVoid();
}
// Timer.isRunning()/isStarted() — the started flag.
// Modern skins gate deferred show/hide on this (Bento suicore/mcvcore:
// `if (callbackTimer.isRunning()) return; if (tempDisable.isRunning()) return;`).
extern "C" scriptVar wq_isRunning(maki_cmd *, int, ScriptObject *o) {
    return makeBoolean(o && wq_timer_isRunning(o) ? 1 : 0);
}
// System.debugString(text, level) — the scripts' own debug log.  Routed to
// stderr under WASABIQT_TRACE_MAKI_DEBUG so a skin's execution path can be
// followed directly (suicore/mcvcore log "switchToMl() {", "showMl() {", …).
extern "C" scriptVar wq_debugString(maki_cmd *, int, ScriptObject *,
                                     scriptVar text, scriptVar /*level*/) {
    if (std::getenv("WASABIQT_TRACE_MAKI_DEBUG") &&
        text.type == SCRIPT_STRING && text.data.sdata)
        std::fprintf(stderr, "[maki] %ls\n", text.data.sdata);
    return makeVoid();
}

// GuiObject / Group / Layer / Layout / Container — geometry stubs.

// Forward decl — body lives further down with the Config stubs.
static ScriptObject *configDummy();

extern "C" scriptVar wq_findObject(maki_cmd *, int, ScriptObject *recv,
                                    scriptVar id) {
    if (std::getenv("WASABIQT_TRACE_MAKI")) {
        fprintf(stderr, "[wq_findObject] type=%d sdata=%p idata=%d\n",
                id.type, id.data.sdata, id.data.idata);
    }
    if (id.type != SCRIPT_STRING || !id.data.sdata)
        return makeObject(nullptr);
    // Scope the search to the RECEIVER's subtree — `g.findObject("text")`
    // must return g's OWN child, not whichever same-id widget last won
    // the flat global map (fixes every InfoLine's value field aliasing
    // the title line's text child).  Falls back to global when the
    // receiver has no such descendant.
    void *handle = wq_widget_findByIdScoped(recv, id.data.sdata);
    if (std::getenv("WASABIQT_TRACE_MAKI")) {
        char nb[128];
        wq_wide_to_ascii(id.data.sdata, nb, sizeof(nb));
        std::fprintf(stderr, "[maki] findObject(%s) -> %p\n", nb, handle);
    }
    // Not found → return real NULL (matches Wasabi: findObject/getObject
    // return MAKE_SCRIPT_OBJECT(NULL) on miss).  Scripts test `if
    // (findObject("x") == NULL)` for absence — the old configDummy made
    // that always false (silent wrong gating).  Returning NULL is safe:
    // method dispatch is name-based (DLFid), so an unguarded
    // `findObject("x").method()` dispatches to wq_method(o=NULL), which
    // every binding null-checks (`if (!o) return …`) → graceful no-op, no
    // guru.  General, no skin ids.
    return makeObject(static_cast<ScriptObject *>(handle));   // handle==null ⇒ NULL
}

extern "C" scriptVar wq_getObject(maki_cmd *m, int n, ScriptObject *o,
                                   scriptVar id) {
    // Pass the RECEIVER through so getObject scopes to the callee's
    // subtree (same as findObject).  Passing nullptr made every
    // `group.getObject("text"/"label")` hit the flat global map, so
    // all InfoLine instances aliased one label/value — only the first
    // line ever positioned.  infoline.maki uses getObject (fileinfo.maki
    // uses findObject), so this is what made the per-instance scoping
    // never take effect for the value-positioning handlers.
    return wq_findObject(m, n, o, id);
}

extern "C" scriptVar wq_setVisible(maki_cmd *, int, ScriptObject *o, scriptVar v) {
    if (!o) return makeVoid();
    const bool on = (v.type == SCRIPT_BOOLEAN || v.type == SCRIPT_INT)
                        ? (v.data.idata != 0) : true;
    wq_widget_setAttr(o, L"visible", on ? L"1" : L"0");
    return makeVoid();
}
// GuiObject.isVisible() — reads the widget's `visible` attr.  Empty/
// missing attr = visible (Wasabi default).  configtabs.m's
// drawer.onTargetReached() switches on `btnClose.isVisible()` to
// decide between the open-completed and close-completed branches;
// without a real reading the close branch always wins and
// ColorThemes.show() never re-fires after a drawer reopen.
extern "C" scriptVar wq_isVisible(maki_cmd *, int, ScriptObject *o) {
    if (!o) return makeBoolean(0);
    const wchar_t *v = wq_widget_getAttr(o, L"visible");
    if (!v) return makeBoolean(1);
    return makeBoolean(std::wcscmp(v, L"0") != 0);
}

extern "C" scriptVar wq_getVisible(maki_cmd *, int, ScriptObject *o) {
    if (!o) return makeBoolean(0);
    const wchar_t *v = wq_widget_getAttr(o, L"visible");
    // visible defaults to "1" when unset.
    if (!v || !*v) return makeBoolean(1);
    return makeBoolean(wcscmp(v, L"0") != 0);
}

// ToggleButton / NStatesButton activated state.  Maki scripts call
// `togglebutton.setActivated(getEqEnabled())` etc. to flip the
// button's `activeImage=` variant.  Routes through wq_widget_setAttr
// → Widget::setXmlParam, which ToggleButtonWidget overrides to
// shadow the `activated` attr onto its typed bool member.  Default
// activated state is false (unactivated).
extern "C" scriptVar wq_setActivated(maki_cmd *, int, ScriptObject *o, scriptVar v) {
    if (!o) return makeVoid();
    const bool on = (v.type == SCRIPT_BOOLEAN || v.type == SCRIPT_INT)
                        ? (v.data.idata != 0) : false;
    wq_widget_setAttr(o, L"activated", on ? L"1" : L"0");
    return makeVoid();
}
extern "C" scriptVar wq_getActivated(maki_cmd *, int, ScriptObject *o) {
    if (!o) return makeBoolean(0);
    const wchar_t *v = wq_widget_getAttr(o, L"activated");
    if (!v || !*v) return makeBoolean(0);
    return makeBoolean(wcscmp(v, L"0") != 0);
}

extern "C" scriptVar wq_setXmlParam(maki_cmd *, int, ScriptObject *o,
                                     scriptVar name, scriptVar value) {
    if (!o) return makeVoid();
    if (name.type != SCRIPT_STRING || !name.data.sdata) return makeVoid();
    const wchar_t *val = (value.type == SCRIPT_STRING && value.data.sdata)
                            ? value.data.sdata : L"";
    if (std::getenv("WASABIQT_TRACE_MAKI")) {
        char nb[128], vb[256];
        wq_wide_to_ascii(name.data.sdata, nb, sizeof(nb));
        wq_wide_to_ascii(val, vb, sizeof(vb));
        // Include the receiver's id so the trace tells us which
        // widget got mutated — same name/value pair often appears for
        // multiple widgets in the same dispatch pass, and without an
        // id you can't tell which one fired.
        char idb[128] = "?";
        const wchar_t *wid = wq_widget_getAttr(o, L"id");
        if (wid) wq_wide_to_ascii(wid, idb, sizeof(idb));
        std::fprintf(stderr, "[maki] setXmlParam id=%s (%s, %s)\n",
                      idb, nb, vb);
    }
    wq_widget_setAttr(o, name.data.sdata, val);
    return makeVoid();
}

extern "C" scriptVar wq_getXmlParam(maki_cmd *, int, ScriptObject *o,
                                     scriptVar name) {
    if (!o || name.type != SCRIPT_STRING || !name.data.sdata)
        return makeString(L"");
    // intern: wq_widget_getAttr returns a shared thread_local buffer that
    // the next getAttr call overwrites; makeString does NOT copy, so an
    // un-interned return dangles once a second getAttr runs (getParam-class
    // UAF — latent today but a trap for any chained-getAttr path/skin).
    return makeString(intern(std::wstring(wq_widget_getAttr(o, name.data.sdata))));
}

extern "C" scriptVar wq_setAlpha(maki_cmd *, int, ScriptObject *, scriptVar) {
    return makeVoid();
}

extern "C" scriptVar wq_getAlpha(maki_cmd *, int, ScriptObject *) {
    return makeInt(255);
}

extern "C" scriptVar wq_getAutoWidth(maki_cmd *, int, ScriptObject *o)  {
    // Wasabi's Text::getPreferences(SUGGESTED_W) measures the rendered
    // text width and adds 4 (per-segment Wasabi convention) plus the
    // widget's lpadding/rpadding.  We additionally add 7 px to bridge
    // the Win32 GDI vs Qt QFontMetrics gap for Arial Bold at the
    // ratio-converted pixel size — without it the titlebar's `lx`
    // centring math reads a smaller text width than the rendered
    // glyphs and the right streak overlaps the title text.  Total
    // adjustment: +11 (computed in wq_widget_textWidth).
    //
    // Non-text widgets (and text widgets with no resolved string)
    // fall back to the declared `w` attribute.
    if (!o) return makeInt(0);
    const int tw = wq_widget_textWidth(o);
    if (tw >= 0) return makeInt(tw);
    // `<layer>` widgets fall back to the bound bitmap's intrinsic
    // width.  By Wasabi convention a layer's preferred width is its
    // bitmap source-rect size; without this, layers declared with
    // only `image=` (no `w=`) return 0 from getAutoWidth — Bento's
    // mainmenu.m piles every menu item at x=0 because of this.
    const int bw = wq_widget_bitmapWidth(o);
    if (bw > 0) return makeInt(bw);
    // A GROUP whose width derives from an `autowidthsource=` label child
    // (the menubar's File/Play/Options/… groups → their txt.menu.* bitmap
    // layer).  Wasabi's getAutoWidth resolves the named child and returns
    // its preferred width; this is what menualign.m's
    // `offset += tmp.getAutoWidth()` relies on to lay the menu items out
    // left-to-right.  Scoped to THIS group's subtree — the Playlist Editor
    // and Media Library menu groups all reuse the child id "label.txt", so
    // a global lookup would size every item from the first one's bitmap.
    if (const wchar_t *src = wq_widget_getAttr(o, L"autowidthsource");
        src && *src) {
        if (void *child = wq_widget_findByIdScoped(o, src)) {
            if (int cbw = wq_widget_bitmapWidth(child); cbw > 0)
                return makeInt(cbw);
            if (int ctw = wq_widget_textWidth(child); ctw > 0)
                return makeInt(ctw);
        }
    }
    return makeInt(wq_widget_getAttrInt(o, L"w"));
}

extern "C" scriptVar wq_getId(maki_cmd *, int, ScriptObject *o) {
    // GuiObject.getId() returns the widget's `id=` attribute.
    // Without this binding the missing-method fallback returns int 0
    // → script-side string operations (e.g. `getToken(l.getId(), ".", 2)`
    // in mainmenu.m's initLL) parse "0" instead of the real id and
    // build wrong widget paths (`menu.layer..normal`) → findObject
    // misses the per-item background layer → menu items render
    // without their highlight overlay → titlebar background looks
    // discontinuous behind Play/Options/View/Help.
    if (!o) return makeString(L"");
    const wchar_t *idStr = wq_widget_getAttr(o, L"id");
    if (std::getenv("WASABIQT_TRACE_MAKI")) {
        char nb[128] = "";
        if (idStr) wq_wide_to_ascii(idStr, nb, sizeof(nb));
        fprintf(stderr, "[maki] getId() -> '%s'\n", nb);
    }
    if (!idStr) return makeString(L"");
    return makeString(intern(std::wstring(idStr)));   // intern: see wq_getXmlParam
}
extern "C" scriptVar wq_getAutoHeight(maki_cmd *, int, ScriptObject *o) {
    if (!o) return makeInt(0);
    const int bh = wq_widget_bitmapHeight(o);
    if (bh > 0) return makeInt(bh);
    return makeInt(wq_widget_getAttrInt(o, L"h"));
}
extern "C" scriptVar wq_getWidth(maki_cmd *, int, ScriptObject *o)      {
    if (!o) return makeInt(0);
    // Wasabi's getWidth returns the EFFECTIVE pixel width.  Prefer the
    // raw `w` attribute when set; otherwise fall back to the widget's
    // own text measurement (text/songticker) or to the autowidthsource-
    // referenced text widget (groups whose width is derived from a
    // label child) so configtabs.m's `tabEQwidth = tEQon.getWidth()`
    // returns a sensible label-driven width.
    int w = wq_widget_getAttrInt(o, L"w");
    if (w > 0) return makeInt(w);
    // relat-sized (w="0" relatw="1") or negative ("parent minus N"):
    // the raw attr is 0/negative, so report the EFFECTIVE resolved
    // pixel width cached by cacheResolvedRects.  This is what makes
    // Bento's centerlayer macro see info.component.holder as ~200px
    // and center the WINAMP logo correctly instead of clipping it.
    if (int rw = wq_widget_resolvedWidth(o); rw > 0) return makeInt(rw);
    if (int tw = wq_widget_textWidth(o); tw > 0) return makeInt(tw);
    const wchar_t *src = wq_widget_getAttr(o, L"autowidthsource");
    if (src && *src) {
        if (void *child = wq_widget_findById(src))
            if (int tw = wq_widget_textWidth(child); tw > 0)
                return makeInt(tw);
    }
    return makeInt(0);
}
extern "C" scriptVar wq_getHeight(maki_cmd *, int, ScriptObject *o)     {
    if (!o) return makeInt(0);
    int h = wq_widget_getAttrInt(o, L"h");
    if (h > 0) return makeInt(h);
    if (int rh = wq_widget_resolvedHeight(o); rh > 0) return makeInt(rh);
    return makeInt(0);
}
// getGuiX/getGuiY — the RAW declared coordinate (may be relat/negative).
extern "C" scriptVar wq_getLeft(maki_cmd *, int, ScriptObject *o)       {
    if (!o) return makeInt(0);
    return makeInt(wq_widget_getAttrInt(o, L"x"));
}
extern "C" scriptVar wq_getTop(maki_cmd *, int, ScriptObject *o)        {
    if (!o) return makeInt(0);
    return makeInt(wq_widget_getAttrInt(o, L"y"));
}
// getLeft/getTop — the RESOLVED on-screen position (the Wasabi API returns
// the live client position, not the declared attr).  Falls back to the raw attr
// when the widget hasn't been resolved yet (pre-layout).  Distinct from
// getGuiX/getGuiY (raw) above — the old code aliased all four to raw, so a
// sibling positioned at getLeft()+N of a relat/anchored widget was placed
// wrong.
// Container.getNumObjects() / enumObject(i) — child enumeration.
extern "C" scriptVar wq_getNumObjects(maki_cmd *, int, ScriptObject *o) {
    return makeInt(o ? wq_widget_childCount(o) : 0);
}
extern "C" scriptVar wq_enumObject(maki_cmd *, int, ScriptObject *o, scriptVar idx) {
    void *c = o ? wq_widget_childAt(o, vargint(idx)) : nullptr;
    return c ? makeObject(static_cast<ScriptObject *>(c)) : makeObject(configDummy());
}
extern "C" scriptVar wq_getResolvedLeft(maki_cmd *, int, ScriptObject *o)  {
    if (!o) return makeInt(0);
    if (wq_widget_hasResolvedRect(o)) return makeInt(wq_widget_resolvedX(o));
    return makeInt(wq_widget_getAttrInt(o, L"x"));
}
extern "C" scriptVar wq_getResolvedTop(maki_cmd *, int, ScriptObject *o)   {
    if (!o) return makeInt(0);
    if (wq_widget_hasResolvedRect(o)) return makeInt(wq_widget_resolvedY(o));
    return makeInt(wq_widget_getAttrInt(o, L"y"));
}

// getGuiW/getGuiH return the RAW declared coordinate (may be relative/
// negative), unlike getWidth/getHeight which resolve to pixels.  Bento's
// pledit.m computes MAX_PL_H = SUI_Y + SUI_H - COMP_Y and needs the raw
// sui.content h="-136" (→ -38), not the resolved 464 (→ +562).  Feeding
// +562 into the enlarged frame's relath="1" h double-resolves to ~1162,
// so the in-player playlist holder overdraws the button bar + cover.
extern "C" scriptVar wq_getGuiW(maki_cmd *, int, ScriptObject *o) {
    return makeInt(o ? wq_widget_getAttrInt(o, L"w") : 0);
}
extern "C" scriptVar wq_getGuiH(maki_cmd *, int, ScriptObject *o) {
    return makeInt(o ? wq_widget_getAttrInt(o, L"h") : 0);
}

extern "C" scriptVar wq_clientToScreenX(maki_cmd *, int, ScriptObject *, scriptVar x) { return x; }
extern "C" scriptVar wq_clientToScreenY(maki_cmd *, int, ScriptObject *, scriptVar y) { return y; }
extern "C" scriptVar wq_screenToClientX(maki_cmd *, int, ScriptObject *, scriptVar x) { return x; }
extern "C" scriptVar wq_screenToClientY(maki_cmd *, int, ScriptObject *, scriptVar y) { return y; }

extern "C" scriptVar wq_getParent(maki_cmd *, int, ScriptObject *o)        {
    // Return the real parent group in the resolved tree (matches Wasabi).
    // centerlayer.m and other scripts position a child relative to
    // `getParent().getWidth()`; returning the widget itself (the old stub)
    // made them center in their own width at the wrong origin.  Fall back
    // to self when the parent is unknown so chained calls never null-guru.
    if (o) if (void *p = wq_widget_parent(o)) return makeObject(static_cast<ScriptObject *>(p));
    return makeObject(o);
}
extern "C" scriptVar wq_getParentLayout(maki_cmd *, int, ScriptObject *o)  {
    // Return the synthetic layout-root pseudo (carries the full
    // layout w/h) rather than the calling widget itself.
    // titlebar.m's resizeObjects needs `l.getWidth() == 354` from
    // the player normal layout, not the title widget's narrower
    // bounds.  Falls back to the calling widget if no layout pseudo
    // is registered (e.g. tests that exercise bindings without a
    // SkinRuntime load pass).
    void *root = wq_layout_root();
    return makeObject(static_cast<ScriptObject *>(root ? root : o));
}
extern "C" scriptVar wq_getParentGroup(maki_cmd *, int, ScriptObject *o)   {
    // Sibling of getParent: return the real parent group, not the widget
    // itself (the old stub).  Same parentWidget back-pointer resolution.
    if (o) if (void *p = wq_widget_parent(o)) return makeObject(static_cast<ScriptObject *>(p));
    return makeObject(o);
}

extern "C" scriptVar wq_bringToFront(maki_cmd *, int, ScriptObject *) { return makeVoid(); }
extern "C" scriptVar wq_bringToBack(maki_cmd *, int, ScriptObject *)  { return makeVoid(); }

// Text.setText(value) — set the widget's displayed string.  Mirrors
// setXmlParam("text", value): TextPainter renders attrs["text"], so
// updating that attr makes the new text appear (+ triggers repaint via
// wq_widget_setAttr).  This is what drives the skin's file-info display
// (fileinfo.maki does t_title.setText(...), t_artist.setText(...), …) —
// it was a no-op before, so every track-info value rendered blank.
// Also fire onTextChanged so dependent layout (infoline.maki repositions
// the value next to the label) updates.
extern "C" scriptVar wq_setText(maki_cmd *, int, ScriptObject *o, scriptVar value) {
    if (!o) return makeVoid();
    const wchar_t *val = (value.type == SCRIPT_STRING && value.data.sdata)
                            ? value.data.sdata : L"";
    if (std::getenv("WASABIQT_TRACE_MAKI") || std::getenv("WASABIQT_TRACE_META")) {
        char idb[128] = "?", vb[256];
        const wchar_t *wid = wq_widget_getAttr(o, L"id");
        if (wid) wq_wide_to_ascii(wid, idb, sizeof(idb));
        wq_wide_to_ascii(val, vb, sizeof(vb));
        std::fprintf(stderr, "[maki] setText id=%s -> '%s'\n", idb, vb);
    }
    wq_widget_setAttr(o, L"text", val);
    // Drive dependent layout: Bento's infoline.maki binds
    // label.onTextChanged to reposition the value field beside the
    // label.  No-op for widgets without an onTextChanged handler.
    qtWasabi::Maki::fireOnTextChangedOnObject(o, val);
    return makeVoid();
}
extern "C" scriptVar wq_getText(maki_cmd *, int, ScriptObject *o) {
    if (!o) return makeString(L"");
    const wchar_t *t = wq_widget_getAttr(o, L"text");
    return makeString(t ? intern(std::wstring(t)) : L"");   // intern: see wq_getXmlParam
}

// ── Config / ConfigItem / ConfigAttribute (stub layer) ─────
//
// The Wasabi script API exposes a Config singleton for managing
// preferences.  initAttribs() chains dozens of Config.newItem() and
// .newAttribute() calls before the rest of the script runs, and every
// missing method on that chain fires a guru meditation.  We do not
// have a real preference store yet, so all of these return a single
// shared dummy ScriptObject that survives dispatch.  setData / getData
// on it become no-ops.  Real Config plumbing is its own milestone.

// Forward-declared from widget-script-object.cpp, in the same namespace.
namespace qtWasabi::Maki { void *createWidgetScriptObject(void *);
                            void tagScriptObjectAsAttribute(void *, const wchar_t *); }

static ScriptObject *configDummy() {
    static ScriptObject *sentinel = static_cast<ScriptObject *>(
        qtWasabi::Maki::createWidgetScriptObject(nullptr));
    return sentinel;
}

// Exposed for the bridge so SCRIPT_OBJECT hydration can fill null vars
// with this fallback. Same singleton as configDummy().
extern "C" void *wq_config_dummy_get() {
    return static_cast<void *>(configDummy());
}

// ── Config service: real per-item instances ─────────────────────
// Config.newItem(name, guid) hands back a ConfigItem-classed instance
// per (guid|name); item.newAttribute(name, default) a ConfigAttribute
// scoped to that item.  Values persist through the preference store
// (scope "cfg:<itemKey>"), so preference-gated skin behaviour survives
// restarts like real Winamp's studio.xnf.

struct CfgItemState {
    std::wstring key;          // canonical store key (guid or name)
    std::wstring name;
};
static std::unordered_map<std::wstring, void *> &cfgItemsByKey() {
    static std::unordered_map<std::wstring, void *> m;
    return m;
}
static std::unordered_map<void *, CfgItemState> &cfgItemState() {
    static std::unordered_map<void *, CfgItemState> m;
    return m;
}
// GUIDs normalise hard (case + braces + spaces are notation); names
// only case-fold — Wasabi item/attribute names legitimately contain
// spaces, and stripping them collapsed distinct attributes into one
// (the Bento songticker regression during bring-up).
static std::wstring cfgGuidNorm(const std::wstring &in) {
    std::wstring out;
    out.reserve(in.size());
    for (wchar_t c : in) {
        if (c == L'{' || c == L'}' || c == L' ') continue;
        if (c >= L'A' && c <= L'Z') c = wchar_t(c - L'A' + L'a');
        out.push_back(c);
    }
    return out;
}
static std::wstring cfgNameNorm(const std::wstring &in) {
    std::wstring out;
    out.reserve(in.size());
    for (wchar_t c : in) {
        if (c >= L'A' && c <= L'Z') c = wchar_t(c - L'A' + L'a');
        out.push_back(c);
    }
    return out;
}
static void *getOrCreateItem(const std::wstring &name,
                             const std::wstring &guid) {
    const std::wstring gk = guid.empty() ? std::wstring()
                                         : L"g:" + cfgGuidNorm(guid);
    const std::wstring nk = name.empty() ? std::wstring()
                                         : L"n:" + cfgNameNorm(name);
    auto &byKey = cfgItemsByKey();
    if (!gk.empty()) {
        auto it = byKey.find(gk);
        if (it != byKey.end()) {
            if (!nk.empty()) byKey[nk] = it->second;   // learn the name
            return it->second;
        }
    }
    if (!nk.empty()) {
        auto it = byKey.find(nk);
        if (it != byKey.end()) {
            if (!gk.empty()) byKey[gk] = it->second;   // learn the guid
            return it->second;
        }
    }
    void *obj = qtWasabi::Maki::createWidgetScriptObject(nullptr);
    qtWasabi::Maki::setScriptObjectClass(
        obj, qtWasabi::Maki::makiClassIndexFromName(L"ConfigItem"));
    CfgItemState st;
    st.key  = !gk.empty() ? gk : nk;
    st.name = name;
    cfgItemState()[obj] = std::move(st);
    if (!gk.empty()) byKey[gk] = obj;
    if (!nk.empty()) byKey[nk] = obj;
    return obj;
}

extern "C" scriptVar wq_newItem(maki_cmd *, int, ScriptObject *,
                                 scriptVar name, scriptVar guid) {
    return makeObject(static_cast<ScriptObject *>(
        getOrCreateItem(vargstr(name), vargstr(guid))));
}
extern "C" scriptVar wq_getItem(maki_cmd *, int, ScriptObject *,
                                scriptVar name) {
    return makeObject(static_cast<ScriptObject *>(
        getOrCreateItem(vargstr(name), std::wstring())));
}
extern "C" scriptVar wq_getItemByGuid(maki_cmd *, int, ScriptObject *,
                                      scriptVar guid) {
    return makeObject(static_cast<ScriptObject *>(
        getOrCreateItem(std::wstring(), vargstr(guid))));
}

// Per-attribute store keyed by the unique ScriptObject the
// newAttribute call hands back.  Each entry holds the attribute's
// item scope, declared name and current value, so getData/setData are
// reflexive per attribute and persist through the preference store.
struct AttrState {
    std::wstring scope;        // "cfg:<itemKey>"
    std::wstring name;
    std::wstring value;
};
static std::unordered_map<void *, AttrState> &attrStore() {
    static std::unordered_map<void *, AttrState> m;
    return m;
}

// (itemScope, attrName) → attribute object, plus a name-only fallback
// index: some scripts declare an attribute on one item handle and look
// it up through another (or through no item at all); resolving by bare
// name keeps those chains connected to the declared default.
static std::unordered_map<std::wstring, void *> &attrByScopedName() {
    static std::unordered_map<std::wstring, void *> m;
    return m;
}
static std::unordered_map<std::wstring, void *> &attrByAnyName() {
    static std::unordered_map<std::wstring, void *> m;
    return m;
}

static std::wstring attrScopeFor(ScriptObject *item) {
    auto it = cfgItemState().find((void *)item);
    if (it != cfgItemState().end()) return L"cfg:" + it->second.key;
    return L"cfg:global";
}

static void *getOrCreateAttr(const std::wstring &scope,
                             const std::wstring &name) {
    const std::wstring sk = scope + L"\x1f" + cfgNameNorm(name);
    auto it = attrByScopedName().find(sk);
    if (it != attrByScopedName().end()) return it->second;
    // Name-only fallback: reconnect cross-item lookups to the
    // declaring attribute instead of minting a value-less twin.
    auto any = attrByAnyName().find(cfgNameNorm(name));
    if (any != attrByAnyName().end()) {
        attrByScopedName()[sk] = any->second;
        return any->second;
    }
    void *obj = qtWasabi::Maki::createWidgetScriptObject(nullptr);
    qtWasabi::Maki::setScriptObjectClass(
        obj, qtWasabi::Maki::makiClassIndexFromName(L"ConfigAttribute"));
    AttrState a;
    a.scope = scope;
    a.name  = name;
    qtWasabi::Maki::tagScriptObjectAsAttribute(obj, name.c_str());
    attrStore()[obj] = std::move(a);
    attrByScopedName()[sk] = obj;
    attrByAnyName()[cfgNameNorm(name)] = obj;
    return obj;
}

extern "C" scriptVar wq_newAttribute(maki_cmd *, int, ScriptObject *o,
                                      scriptVar name, scriptVar def) {
    const std::wstring nm = vargstr(name);
    void *obj = getOrCreateAttr(attrScopeFor(o), nm);
    AttrState &a = attrStore()[obj];
    // A persisted user value wins over the script's declared default.
    std::wstring stored;
    if (prefLoad(a.scope.c_str(), a.name, stored))
        a.value = stored;
    else
        a.value = vargstr(def);
    if (std::getenv("WASABIQT_TRACE_ATTRIB"))
        std::fprintf(stderr,
                     "[attrib] newAttribute this=%p scope='%ls' name='%ls' val='%ls'\n",
                     obj, a.scope.c_str(), nm.c_str(), a.value.c_str());
    return makeObject(static_cast<ScriptObject *>(obj));
}
extern "C" scriptVar wq_getAttribute(maki_cmd *, int, ScriptObject *o,
                                      scriptVar name) {
    const std::wstring nm = vargstr(name);
    if (nm.empty()) return makeObject(configDummy());
    return makeObject(static_cast<ScriptObject *>(
        getOrCreateAttr(attrScopeFor(o), nm)));
}
extern "C" scriptVar wq_setData(maki_cmd *, int, ScriptObject *o,
                                 scriptVar v) {
    auto it = attrStore().find((void *)o);
    if (it != attrStore().end()) {
        if (v.type == SCRIPT_STRING && v.data.sdata) {
            it->second.value = v.data.sdata;
            prefSave(it->second.scope.c_str(), it->second.name,
                     it->second.value);
        }
        if (std::getenv("WASABIQT_TRACE_GETDATA"))
            std::fprintf(stderr, "[setdata] '%ls' | '%ls' <- '%ls'\n",
                it->second.scope.c_str(), it->second.name.c_str(),
                it->second.value.c_str());
    }
    // By the Wasabi API, a config attribute's setData() fires the
    // onDataChanged event so bound script handlers re-apply the new
    // value (pledit.m guards re-entrancy with `attrib_bypass`).  A
    // shallow depth guard protects pathological skins from a runaway
    // setData→onDataChanged→setData chain.
    static int s_depth = 0;
    if (o && s_depth < 32) {
        ++s_depth;
        qtWasabi::Maki::fireZeroArgEventOnObject(o, L"onDataChanged");
        --s_depth;
    }
    return makeVoid();
}
extern "C" scriptVar wq_getData(maki_cmd *, int, ScriptObject *o) {
    auto it = attrStore().find((void *)o);
    if (std::getenv("WASABIQT_TRACE_GETDATA")) {
        if (it == attrStore().end())
            std::fprintf(stderr, "[getdata] obj=%p UNKNOWN -> ''\n", (void*)o);
        else
            std::fprintf(stderr, "[getdata] '%ls' | '%ls' -> '%ls'\n",
                it->second.scope.c_str(), it->second.name.c_str(),
                it->second.value.c_str());
    }
    if (it == attrStore().end()) return makeString(L"");
    return makeString(intern(it->second.value));
}
// ConfigAttribute.onDataChanged() — when a script *calls* this method
// (e.g. pledit.m's `playlist_enlarge_attrib.onDataChanged()` at load to
// apply the default), the VM routes through callDLF, which invokes this
// native fn.  In real Winamp the native event method re-dispatches to
// the script's own bound handler; we do the same via the receiver-gated
// fire helper.  Without this binding callDLF saw a NULL e->ptr and the
// handler body never ran (the playlist never enlarged, drawers never
// animated, etc.).  Engine-level: fixes every `obj.onDataChanged()`
// self-call in any skin, no per-skin glue.
extern "C" scriptVar wq_onDataChanged(maki_cmd *, int, ScriptObject *o) {
    if (o) qtWasabi::Maki::fireZeroArgEventOnObject(o, L"onDataChanged");
    return makeVoid();
}

// WinampConfigGroup.getInt(key) — no config store yet, return 0.
extern "C" scriptVar wq_getInt(maki_cmd *, int, ScriptObject *, scriptVar) {
    return makeInt(0);
}

// Layer/Button/Slider.setEnabled — mutate enabled= attr (no-op for
// widgets that don't render enabled state yet, harmless for those
// that do).
extern "C" scriptVar wq_setEnabled(maki_cmd *, int, ScriptObject *o, scriptVar v) {
    if (!o) return makeVoid();
    const bool on = (v.type == SCRIPT_BOOLEAN || v.type == SCRIPT_INT)
                        ? (v.data.idata != 0) : true;
    wq_widget_setAttr(o, L"enabled", on ? L"1" : L"0");
    return makeVoid();
}

// Slider.getPosition — read position= as int (0..255 by Wasabi
// convention).  Without a real slider model we just return 0; scripts
// reading this typically only need a non-null receiver.
extern "C" scriptVar wq_getPosition(maki_cmd *, int, ScriptObject *o) {
    if (!o) return makeInt(0);
    // Wasabi:Frame: return the LIVE divider position (pullbarpos), stored as
    // `_frame_divpos` and updated by setPosition — the Maki Frame divider-
    // position semantics.  Planted at expansion (init = declared width/
    // height), so it's correct at load AND tracks setPosition/drag.  Detect
    // "is a frame" by the attr's presence (a frame's divpos can legitimately
    // be 0).
    const wchar_t *dp = wq_widget_getAttr(o, L"_frame_divpos");
    if (dp && *dp) {
        const int v = wq_widget_getAttrInt(o, L"_frame_divpos");
        if (std::getenv("WASABIQT_TRACE_PLENLARGE")) {
            const wchar_t *id = wq_widget_getAttr(o, L"id");
            std::fprintf(stderr, "[plenlarge] getPosition(frame %ls) -> %d\n",
                         id ? id : L"?", v);
        }
        return makeInt(v);
    }
    // Otherwise a Slider: prefer the LIVE host value (0..255) for this
    // slider's action — the Maki Slider.getPosition value space — falling
    // back to the stored `position` attr when the action has no host value.
    const int hostVal = wq_slider_get_position(o);
    if (hostVal >= 0) return makeInt(hostVal);
    return makeInt(wq_widget_getAttrInt(o, L"position"));
}
// setPosition(pos) — TWO receivers share this name in the Maki API.  A
// Wasabi:Frame (carries `_frame_divpos`) moves its live divider + re-splits;
// a Slider sets its 0..255 value and pushes it to the host (so a scripted
// balance/volume button drives the audio).  Discriminate by the frame attr so
// the Frame divider path (and the playlist-show timer chain) stays untouched.
extern "C" scriptVar wq_setPosition(maki_cmd *, int, ScriptObject *o, scriptVar pos) {
    if (o) {
        int p = 0;
        if (pos.type == SCRIPT_INT) p = pos.data.idata;
        else if (pos.type == SCRIPT_STRING && pos.data.sdata)
            p = static_cast<int>(std::wcstol(pos.data.sdata, nullptr, 10));
        const wchar_t *dp = wq_widget_getAttr(o, L"_frame_divpos");
        if (dp && *dp) {
            wq_widget_setFrameDivider(o, p);   // Wasabi:Frame divider
        } else {
            if (p < 0) p = 0; else if (p > 255) p = 255;
            wchar_t buf[16];
            std::swprintf(buf, 16, L"%d", p);
            wq_widget_setAttr(o, L"position", buf);
            wq_slider_set_position(o, p);       // Slider value → host action
        }
    }
    return makeVoid();
}

// Class-scoped position bodies.  The class registry dispatches these
// per the DECLARED class of the .maki import, so neither needs the
// attr-sniffing discrimination above (which stays as the flat-table
// fallback for imports whose class we do not know).
extern "C" scriptVar wq_sliderGetPosition(maki_cmd *, int, ScriptObject *o) {
    if (!o) return makeInt(0);
    const int hostVal = wq_slider_get_position(o);
    if (hostVal >= 0) return makeInt(hostVal);
    return makeInt(wq_widget_getAttrInt(o, L"position"));
}
extern "C" scriptVar wq_sliderSetPosition(maki_cmd *, int, ScriptObject *o,
                                          scriptVar pos) {
    if (o) {
        int p = 0;
        if (pos.type == SCRIPT_INT) p = pos.data.idata;
        else if (pos.type == SCRIPT_STRING && pos.data.sdata)
            p = static_cast<int>(std::wcstol(pos.data.sdata, nullptr, 10));
        if (p < 0) p = 0; else if (p > 255) p = 255;
        wchar_t buf[16];
        std::swprintf(buf, 16, L"%d", p);
        wq_widget_setAttr(o, L"position", buf);
        wq_slider_set_position(o, p);
    }
    return makeVoid();
}
extern "C" scriptVar wq_frameGetPosition(maki_cmd *, int, ScriptObject *o) {
    if (!o) return makeInt(0);
    return makeInt(wq_widget_getAttrInt(o, L"_frame_divpos"));
}
extern "C" scriptVar wq_frameSetPosition(maki_cmd *, int, ScriptObject *o,
                                         scriptVar pos) {
    if (o) {
        int p = 0;
        if (pos.type == SCRIPT_INT) p = pos.data.idata;
        else if (pos.type == SCRIPT_STRING && pos.data.sdata)
            p = static_cast<int>(std::wcstol(pos.data.sdata, nullptr, 10));
        wq_widget_setFrameDivider(o, p);
    }
    return makeVoid();
}

// Container.getLayout(name) — return a layout-root-shaped pseudo so
// callers can chain getWidth()/getHeight() against something.  Real
// Wasabi looks up the named layout within the container; we don't
// have a container model yet, so reuse the global layout root.
extern "C" scriptVar wq_getLayoutByName(maki_cmd *, int, ScriptObject *o,
                                          scriptVar /*name*/) {
    void *root = wq_layout_root();
    return makeObject(static_cast<ScriptObject *>(root ? root : o));
}

// Wac/Container singleton lookups — hand back the layout-root pseudo
// so chained calls (.getLayout(...).getWidth() etc.) succeed.
extern "C" scriptVar wq_getContainer(maki_cmd *, int, ScriptObject *o,
                                       scriptVar /*name*/) {
    void *root = wq_layout_root();
    return makeObject(static_cast<ScriptObject *>(root ? root : o));
}

extern "C" scriptVar wq_findWac(maki_cmd *, int, ScriptObject *o,
                                  scriptVar /*guid*/) {
    void *root = wq_layout_root();
    return makeObject(static_cast<ScriptObject *>(root ? root : o));
}

// SystemObject.newDynamicContainer(name) — Wasabi creates a fresh
// runtime container.  Hand back the layout-root pseudo so chained
// .getLayout(...).findObject(...) calls succeed.
extern "C" scriptVar wq_newDynamicContainer(maki_cmd *, int, ScriptObject *o,
                                              scriptVar /*name*/) {
    void *root = wq_layout_root();
    return makeObject(static_cast<ScriptObject *>(root ? root : o));
}

// Text.setFontSize / Layer.setFontSize — mutate fontsize= attr.
extern "C" scriptVar wq_setFontSize(maki_cmd *, int, ScriptObject *o, scriptVar v) {
    if (!o) return makeVoid();
    int n = 0;
    switch (v.type) {
        case SCRIPT_INT:
        case SCRIPT_BOOLEAN: n = v.data.idata; break;
        case SCRIPT_FLOAT:   n = static_cast<int>(v.data.fdata); break;
        case SCRIPT_DOUBLE:  n = static_cast<int>(v.data.ddata); break;
        default: n = 0;
    }
    wchar_t buf[16];
    std::swprintf(buf, 16, L"%d", n);
    wq_widget_setAttr(o, L"fontsize", buf);
    return makeVoid();
}

// ── Typed instances: List, BitList, Map, Region ──────────────────
// `new List` / `new Map` etc. now construct class-stamped objects
// (ObjectTable::instantiate); these bodies give them their real
// behaviour, keyed by the receiver handle.  List/BitList are pure
// value stores here; Map/Region pixel and region state lives on the
// Qt side of the bridge (SkinRuntimeBridge.cpp wq_map_*/wq_region_*).

extern "C" {
int  wq_map_load(void *o, const wchar_t *bitmapId);
int  wq_map_value(void *o, int x, int y);
int  wq_map_argb(void *o, int x, int y, int which);
int  wq_map_w(void *o);
int  wq_map_h(void *o);
int  wq_map_in_region(void *o, int x, int y);
void wq_map_region_into(void *mapObj, void *regionObj);
void wq_region_add(void *dst, void *src);
void wq_region_sub(void *dst, void *src);
void wq_region_offset(void *o, int x, int y);
void wq_region_stretch(void *o, double f);
void wq_region_copy(void *dst, void *src);
int  wq_region_load_bitmap(void *o, const wchar_t *bitmapId);
int  wq_region_load_map(void *regionObj, void *mapObj, int byteThresh,
                        int inverted);
int  wq_region_bbox(void *o, int which);
void wq_typed_state_free(void *o);
void wq_timer_kill(void *timerSO);
}

namespace {

// One stored List slot.  Strings are copied (VM string temporaries
// die at statement end); objects are held by pointer, matching the
// reference's raw scriptVar storage.
struct MakiListVal {
    int          type = SCRIPT_VOID;
    int          i    = 0;
    double       d    = 0.0;
    std::wstring s;
    void        *obj  = nullptr;
};

std::map<void *, std::vector<MakiListVal>> g_makiLists;
std::map<void *, std::vector<bool>>        g_makiBitLists;

MakiListVal listValFrom(const scriptVar &v) {
    MakiListVal out;
    out.type = v.type;
    switch (v.type) {
    case SCRIPT_INT:
    case SCRIPT_BOOLEAN: out.i = v.data.idata; break;
    case SCRIPT_FLOAT:   out.d = v.data.fdata; break;
    case SCRIPT_DOUBLE:  out.d = v.data.ddata; break;
    case SCRIPT_STRING:  out.s = v.data.sdata ? v.data.sdata : L""; break;
    case SCRIPT_OBJECT:  out.obj = v.data.odata; break;
    default: break;
    }
    return out;
}

scriptVar listValTo(const MakiListVal &v) {
    switch (v.type) {
    case SCRIPT_INT:     return makeInt(v.i);
    case SCRIPT_BOOLEAN: return makeBoolean(v.i);
    case SCRIPT_FLOAT:   return makeFloat(float(v.d));
    case SCRIPT_DOUBLE:  return makeDouble(v.d);
    case SCRIPT_STRING:  return makeString(intern(v.s));
    case SCRIPT_OBJECT:
        return makeObject(static_cast<ScriptObject *>(v.obj));
    default:             return makeVoid();
    }
}

// findItem equality: strings compare case-insensitively (Wasabi
// convention); everything else mirrors the reference's raw compare.
bool listValEq(const MakiListVal &a, const scriptVar &b) {
    if (a.type == SCRIPT_STRING && b.type == SCRIPT_STRING) {
        const wchar_t *x = a.s.c_str();
        const wchar_t *y = b.data.sdata ? b.data.sdata : L"";
        while (*x && *y) {
            wchar_t cx = *x, cy = *y;
            if (cx >= L'A' && cx <= L'Z') cx = wchar_t(cx - L'A' + L'a');
            if (cy >= L'A' && cy <= L'Z') cy = wchar_t(cy - L'A' + L'a');
            if (cx != cy) return false;
            ++x; ++y;
        }
        return *x == 0 && *y == 0;
    }
    if (a.type != b.type) return false;
    switch (a.type) {
    case SCRIPT_INT:
    case SCRIPT_BOOLEAN: return a.i == b.data.idata;
    case SCRIPT_FLOAT:   return a.d == double(b.data.fdata);
    case SCRIPT_DOUBLE:  return a.d == b.data.ddata;
    case SCRIPT_OBJECT:  return a.obj == b.data.odata;
    default:             return false;
    }
}

double numArg(const scriptVar &v) {
    switch (v.type) {
    case SCRIPT_INT:
    case SCRIPT_BOOLEAN: return double(v.data.idata);
    case SCRIPT_FLOAT:   return double(v.data.fdata);
    case SCRIPT_DOUBLE:  return v.data.ddata;
    case SCRIPT_STRING:
        return v.data.sdata ? std::wcstod(v.data.sdata, nullptr) : 0.0;
    default:             return 0.0;
    }
}
int intArg(const scriptVar &v) { return int(numArg(v)); }

}  // namespace

// PopupMenu — instance state keyed by ScriptObject*, like List.  The
// Qt-side popup (QMenu at the cursor) lives behind the wq_menu_*
// bridge implemented in SkinRuntimeBridge.cpp; this side only keeps
// the command list a script builds via addCommand/addSeparator.
struct MakiMenuItem {
    std::wstring text;
    int  id       = 0;
    bool checked  = false;
    bool disabled = false;
    bool separator = false;
};
static std::unordered_map<ScriptObject *, std::vector<MakiMenuItem>>
    g_makiMenus;

extern "C" void wq_menu_bridge_begin();
extern "C" void wq_menu_bridge_add(const wchar_t *text, int id,
                                   int checked, int disabled,
                                   int separator);
extern "C" int  wq_menu_bridge_exec(int atMouse, int x, int y);

extern "C" scriptVar wq_menuAddCommand(maki_cmd *, int, ScriptObject *o,
                                       scriptVar txt, scriptVar id,
                                       scriptVar checked,
                                       scriptVar disabled) {
    if (o) {
        MakiMenuItem it;
        it.text = (txt.type == SCRIPT_STRING && txt.data.sdata)
                      ? txt.data.sdata : L"";
        it.id       = intArg(id);
        it.checked  = intArg(checked)  != 0;
        it.disabled = intArg(disabled) != 0;
        g_makiMenus[o].push_back(std::move(it));
    }
    return makeVoid();
}
extern "C" scriptVar wq_menuAddSeparator(maki_cmd *, int,
                                         ScriptObject *o) {
    if (o) {
        MakiMenuItem it;
        it.separator = true;
        g_makiMenus[o].push_back(std::move(it));
    }
    return makeVoid();
}
extern "C" scriptVar wq_menuGetNumCommands(maki_cmd *, int,
                                           ScriptObject *o) {
    auto it = g_makiMenus.find(o);
    return makeInt(it == g_makiMenus.end() ? 0
                                           : int(it->second.size()));
}
extern "C" scriptVar wq_menuCheckCommand(maki_cmd *, int, ScriptObject *o,
                                         scriptVar id, scriptVar on) {
    if (o) {
        auto it = g_makiMenus.find(o);
        if (it != g_makiMenus.end())
            for (auto &c : it->second)
                if (c.id == intArg(id)) c.checked = intArg(on) != 0;
    }
    return makeVoid();
}
extern "C" scriptVar wq_menuDisableCommand(maki_cmd *, int,
                                           ScriptObject *o,
                                           scriptVar id, scriptVar dis) {
    if (o) {
        auto it = g_makiMenus.find(o);
        if (it != g_makiMenus.end())
            for (auto &c : it->second)
                if (c.id == intArg(id)) c.disabled = intArg(dis) != 0;
    }
    return makeVoid();
}
static scriptVar menuPop(ScriptObject *o, int atMouse, int x, int y) {
    auto it = g_makiMenus.find(o);
    if (it == g_makiMenus.end() || it->second.empty()) return makeInt(0);
    wq_menu_bridge_begin();
    for (const auto &c : it->second)
        wq_menu_bridge_add(c.text.c_str(), c.id, c.checked ? 1 : 0,
                           c.disabled ? 1 : 0, c.separator ? 1 : 0);
    // Wasabi popAtMouse blocks until the user picks a command and
    // returns its id; a dismissed menu returns 0 (script guards like
    // wa2songtimer's `timermode >= 1 && <= 2` rely on that).
    return makeInt(wq_menu_bridge_exec(atMouse, x, y));
}
extern "C" scriptVar wq_menuPopAtMouse(maki_cmd *, int, ScriptObject *o) {
    return menuPop(o, 1, 0, 0);
}
extern "C" scriptVar wq_menuPopAtXY(maki_cmd *, int, ScriptObject *o,
                                    scriptVar x, scriptVar y) {
    return menuPop(o, 0, intArg(x), intArg(y));
}

// List
extern "C" scriptVar wq_listAddItem(maki_cmd *, int, ScriptObject *o,
                                    scriptVar v) {
    if (o) g_makiLists[o].push_back(listValFrom(v));
    return makeVoid();
}
extern "C" scriptVar wq_listRemoveItem(maki_cmd *, int, ScriptObject *o,
                                       scriptVar pos) {
    if (o) {
        auto &l = g_makiLists[o];
        const int i = intArg(pos);
        if (i >= 0 && size_t(i) < l.size()) l.erase(l.begin() + i);
    }
    return makeVoid();
}
extern "C" scriptVar wq_listEnumItem(maki_cmd *, int, ScriptObject *o,
                                     scriptVar pos) {
    if (o) {
        auto it = g_makiLists.find(o);
        if (it != g_makiLists.end()) {
            const int i = intArg(pos);
            if (i >= 0 && size_t(i) < it->second.size())
                return listValTo(it->second[size_t(i)]);
        }
    }
    return makeVoid();
}
extern "C" scriptVar wq_listGetNumItems(maki_cmd *, int, ScriptObject *o) {
    auto it = g_makiLists.find(o);
    return makeInt(it == g_makiLists.end() ? 0 : int(it->second.size()));
}
extern "C" scriptVar wq_listFindItem2(maki_cmd *, int, ScriptObject *o,
                                      scriptVar v, scriptVar start) {
    if (o) {
        auto it = g_makiLists.find(o);
        if (it != g_makiLists.end()) {
            for (size_t i = size_t(intArg(start) > 0 ? intArg(start) : 0);
                 i < it->second.size(); ++i)
                if (listValEq(it->second[i], v)) return makeInt(int(i));
        }
    }
    return makeInt(-1);
}
extern "C" scriptVar wq_listFindItem(maki_cmd *c, int vsd, ScriptObject *o,
                                     scriptVar v) {
    return wq_listFindItem2(c, vsd, o, v, makeInt(0));
}
extern "C" scriptVar wq_listRemoveAll(maki_cmd *, int, ScriptObject *o) {
    if (o) g_makiLists.erase(o);
    return makeVoid();
}

// BitList
extern "C" scriptVar wq_bitlistGetItem(maki_cmd *, int, ScriptObject *o,
                                       scriptVar pos) {
    auto it = g_makiBitLists.find(o);
    if (it != g_makiBitLists.end()) {
        const int i = intArg(pos);
        if (i >= 0 && size_t(i) < it->second.size())
            return makeBoolean(it->second[size_t(i)]);
    }
    return makeBoolean(0);
}
extern "C" scriptVar wq_bitlistSetItem(maki_cmd *, int, ScriptObject *o,
                                       scriptVar pos, scriptVar val) {
    if (o) {
        auto &b = g_makiBitLists[o];
        const int i = intArg(pos);
        if (i >= 0) {
            if (size_t(i) >= b.size()) b.resize(size_t(i) + 1, false);
            b[size_t(i)] = intArg(val) != 0;
        }
    }
    return makeVoid();
}
extern "C" scriptVar wq_bitlistSetSize(maki_cmd *, int, ScriptObject *o,
                                       scriptVar n) {
    if (o) {
        const int c = intArg(n);
        g_makiBitLists[o].assign(size_t(c > 0 ? c : 0), false);
    }
    return makeVoid();
}
extern "C" scriptVar wq_bitlistGetSize(maki_cmd *, int, ScriptObject *o) {
    auto it = g_makiBitLists.find(o);
    return makeInt(it == g_makiBitLists.end() ? 0 : int(it->second.size()));
}

// Map
extern "C" scriptVar wq_mapLoadMap(maki_cmd *, int, ScriptObject *o,
                                   scriptVar id) {
    if (o && id.type == SCRIPT_STRING && id.data.sdata)
        wq_map_load(o, id.data.sdata);
    return makeVoid();
}
extern "C" scriptVar wq_mapGetValue(maki_cmd *, int, ScriptObject *o,
                                    scriptVar x, scriptVar y) {
    return makeInt(wq_map_value(o, intArg(x), intArg(y)));
}
extern "C" scriptVar wq_mapGetARGBValue(maki_cmd *, int, ScriptObject *o,
                                        scriptVar x, scriptVar y,
                                        scriptVar which) {
    return makeInt(wq_map_argb(o, intArg(x), intArg(y), intArg(which)));
}
extern "C" scriptVar wq_mapInRegion(maki_cmd *, int, ScriptObject *o,
                                    scriptVar x, scriptVar y) {
    return makeBoolean(wq_map_in_region(o, intArg(x), intArg(y)));
}
extern "C" scriptVar wq_mapGetWidth(maki_cmd *, int, ScriptObject *o) {
    return makeInt(wq_map_w(o));
}
extern "C" scriptVar wq_mapGetHeight(maki_cmd *, int, ScriptObject *o) {
    return makeInt(wq_map_h(o));
}
extern "C" scriptVar wq_mapGetRegion(maki_cmd *, int, ScriptObject *o) {
    // Hand back a Region-classed object carrying a copy of the map's
    // region — SMap::getRegion semantics.
    void *region = qtWasabi::Maki::createWidgetScriptObject(nullptr);
    qtWasabi::Maki::setScriptObjectClass(
        region, qtWasabi::Maki::makiClassIndexFromName(L"Region"));
    wq_map_region_into(o, region);
    return makeObject(static_cast<ScriptObject *>(region));
}

// Region
extern "C" scriptVar wq_regionAdd(maki_cmd *, int, ScriptObject *o,
                                  scriptVar r) {
    if (o && r.type == SCRIPT_OBJECT) wq_region_add(o, r.data.odata);
    return makeVoid();
}
extern "C" scriptVar wq_regionSub(maki_cmd *, int, ScriptObject *o,
                                  scriptVar r) {
    if (o && r.type == SCRIPT_OBJECT) wq_region_sub(o, r.data.odata);
    return makeVoid();
}
extern "C" scriptVar wq_regionOffset(maki_cmd *, int, ScriptObject *o,
                                     scriptVar x, scriptVar y) {
    if (o) wq_region_offset(o, intArg(x), intArg(y));
    return makeVoid();
}
extern "C" scriptVar wq_regionStretch(maki_cmd *, int, ScriptObject *o,
                                      scriptVar f) {
    if (o) wq_region_stretch(o, numArg(f));
    return makeVoid();
}
extern "C" scriptVar wq_regionCopy(maki_cmd *, int, ScriptObject *o,
                                   scriptVar r) {
    if (o && r.type == SCRIPT_OBJECT) wq_region_copy(o, r.data.odata);
    return makeVoid();
}
extern "C" scriptVar wq_regionLoadFromBitmap(maki_cmd *, int,
                                             ScriptObject *o,
                                             scriptVar id) {
    if (o && id.type == SCRIPT_STRING && id.data.sdata)
        wq_region_load_bitmap(o, id.data.sdata);
    return makeVoid();
}
extern "C" scriptVar wq_regionLoadFromMap(maki_cmd *, int, ScriptObject *o,
                                          scriptVar map, scriptVar byteT,
                                          scriptVar inv) {
    if (o && map.type == SCRIPT_OBJECT)
        wq_region_load_map(o, map.data.odata, intArg(byteT), intArg(inv));
    return makeVoid();
}
extern "C" scriptVar wq_regionGetBoundingBoxX(maki_cmd *, int, ScriptObject *o) {
    return makeInt(wq_region_bbox(o, 0));
}
extern "C" scriptVar wq_regionGetBoundingBoxY(maki_cmd *, int, ScriptObject *o) {
    return makeInt(wq_region_bbox(o, 1));
}
extern "C" scriptVar wq_regionGetBoundingBoxW(maki_cmd *, int, ScriptObject *o) {
    return makeInt(wq_region_bbox(o, 2));
}
extern "C" scriptVar wq_regionGetBoundingBoxH(maki_cmd *, int, ScriptObject *o) {
    return makeInt(wq_region_bbox(o, 3));
}

namespace qtWasabi::Maki {

// OPCODE_DELETE / removeScript teardown for typed instances.  The
// ScriptObject itself stays alive (other VM variables may still hold
// the pointer — see the skin-switch use-after-free history); only the
// typed backing state is released, which also fixes the deleted-Timer
// zombie (its QTimer kept firing until the dead-root guard hit).
void makiTypedDestroy(void *o, int classIdx) {
    if (!o) return;
    static const int kTimerIdx = makiClassIndexFromName(L"Timer");
    if (classIdx >= 0 && classIdx == kTimerIdx) wq_timer_kill(o);
    g_makiLists.erase(o);
    g_makiBitLists.erase(o);
    wq_typed_state_free(o);
}

}  // namespace qtWasabi::Maki

// ── method registry ─────────────────────────────────────────────

namespace qtWasabi::Maki {

struct MakiMethod { const wchar_t *name; int nparams; void *ptr; };

// Looked up by name in the new addrefDLF.  Entries with ptr=nullptr
// fall through to the safe no-op path (e->ptr stays NULL, CALLM
// returns int 0).
const MakiMethod *makiMethodTable(int *count) {
    static const MakiMethod kMethods[] = {
        // SystemObject
        {L"getRuntimeVersion",       0, (void *)wq_getRuntimeVersion},
        {L"getSkinName",             0, (void *)wq_getSkinName},
        // SApplication
        {L"GetApplicationName",      0, (void *)wq_appGetName},
        {L"GetVersionString",        0, (void *)wq_appGetVersionString},
        {L"GetVersionNumberString",  0, (void *)wq_appGetVersionNumberString},
        {L"GetBuildNumber",          0, (void *)wq_appGetBuildNumber},
        {L"getDate",                 0, (void *)wq_getDate},
        {L"getTimeOfDay",            0, (void *)wq_getTimeOfDay},
        {L"getDateYear",  1, (void *)wq_getDateYear},  {L"getDateMonth", 1, (void *)wq_getDateMonth},
        {L"getDateDay",   1, (void *)wq_getDateDay},   {L"getDateDow",   1, (void *)wq_getDateDow},
        {L"getDateDoy",   1, (void *)wq_getDateDoy},   {L"getDateDst",   1, (void *)wq_getDateDst},
        {L"getDateHour",  1, (void *)wq_getDateHour},  {L"getDateMin",   1, (void *)wq_getDateMin},
        {L"getDateSec",   1, (void *)wq_getDateSec},
        {L"isTransparencyAvailable", 0, (void *)wq_isTransparencyAvailable},
        {L"getParam",                0, (void *)wq_getParam},
        {L"getToken",                3, (void *)wq_getToken},
        {L"strsearch",               2, (void *)wq_strsearch},
        {L"strlen",                  1, (void *)wq_strlen},
        {L"strleft",                 2, (void *)wq_strleft},
        {L"strright",                2, (void *)wq_strright},
        {L"strmid",                  3, (void *)wq_strmid},
        {L"strlower",                1, (void *)wq_strlower},
        {L"strupper",                1, (void *)wq_strupper},
        {L"integerToTime",           1, (void *)wq_integerToTime},
        {L"integerToLongTime",       1, (void *)wq_integerToLongTime},
        {L"floatToString",           2, (void *)wq_floatToString},
        {L"stringToFloat",           1, (void *)wq_stringToFloat},
        {L"random",                  1, (void *)wq_random},
        {L"sin",   1, (void *)wq_sin},   {L"cos",   1, (void *)wq_cos},
        {L"tan",   1, (void *)wq_tan},   {L"asin",  1, (void *)wq_asin},
        {L"acos",  1, (void *)wq_acos},  {L"atan",  1, (void *)wq_atan},
        {L"atan2", 2, (void *)wq_atan2}, {L"pow",   2, (void *)wq_pow},
        {L"sqr",   1, (void *)wq_sqr},   {L"sqrt",  1, (void *)wq_sqrt},
        {L"ln",    1, (void *)wq_ln},    {L"log10", 1, (void *)wq_log10},
        {L"integer", 1, (void *)wq_integer}, {L"frac", 1, (void *)wq_frac},
        {L"translate",               1, (void *)wq_translate},
        {L"stringToInteger",         1, (void *)wq_stringToInteger},
        {L"integerToString",         1, (void *)wq_integerToString},
        {L"messageBox",              4, (void *)wq_messageBox},
        {L"navigateUrlBrowser",      1, (void *)wq_navigateUrlBrowser},
        {L"getPrivateInt",           3, (void *)wq_getPrivateInt},
        {L"setPrivateInt",           3, (void *)wq_setPrivateInt},
        {L"getPrivateString",        3, (void *)wq_getPrivateString},
        {L"setPrivateString",        3, (void *)wq_setPrivateString},
        {L"getPublicString",         2, (void *)wq_getPublicString},
        {L"setPublicString",         2, (void *)wq_setPublicString},
        {L"getSongInfoText",         0, (void *)wq_getSongInfoText},
        {L"getSongInfoTextTranslated", 0, (void *)wq_getSongInfoText},
        {L"getPlayItemLength",       0, (void *)wq_getPlayItemLength},
        {L"getPlaylistLength",       0, (void *)wq_getPlaylistLength},
        {L"getPlaylistIndex",        0, (void *)wq_getPlaylistIndex},
        {L"getItemByGuid",           1, (void *)wq_getItemByGuid},
        {L"getPublicInt",            2, (void *)wq_getPublicInt},
        {L"setPublicInt",            2, (void *)wq_setPublicInt},
        {L"getScriptGroup",          0, (void *)wq_getScriptGroup},
        {L"getPlayItemMetaDataString", 1, (void *)wq_getPlayItemMetaDataString},
        {L"getPlayItemDisplayTitle", 0, (void *)wq_getPlayItemDisplayTitle},
        {L"getPlayItemString",       0, (void *)wq_getPlayItemString},
        {L"getDecoderName",          1, (void *)wq_getDecoderName},
        {L"getFileName",             1, (void *)wq_getFileName},
        {L"getExtFamily",            1, (void *)wq_getExtFamily},
        {L"removePath",              1, (void *)wq_removePath},
        {L"getExtension",            1, (void *)wq_getExtension},
        {L"setDelay",                1, (void *)wq_setDelay},
        {L"start",                   0, (void *)wq_timerStart},
        {L"isRunning",               0, (void *)wq_isRunning},
        {L"isStarted",               0, (void *)wq_isRunning},
        {L"debugString",             2, (void *)wq_debugString},
        {L"show",                    0, (void *)wq_show},
        {L"hide",                    0, (void *)wq_hide},
        {L"leftClick",               0, (void *)wq_leftClick},
        {L"rightClick",              0, (void *)wq_rightClick},
        {L"setEqBand",               2, (void *)wq_setEqBand},
        {L"getEqBand",               1, (void *)wq_getEqBand},
        {L"setVolume",               1, (void *)wq_setVolume},
        {L"getVolume",               0, (void *)wq_getVolume},
        {L"stop",                    0, (void *)wq_stop},
        // GuiObject family
        {L"findObject",              1, (void *)wq_findObject},
        {L"getObject",               1, (void *)wq_getObject},
        {L"setVisible",              1, (void *)wq_setVisible},
        {L"isVisible",               0, (void *)wq_isVisible},
        {L"getVisible",              0, (void *)wq_getVisible},
        {L"setActivated",            1, (void *)wq_setActivated},
        {L"getActivated",            0, (void *)wq_getActivated},
        {L"setXmlParam",             2, (void *)wq_setXmlParam},
        // Maki is case-sensitive on method lookup; scripts use any
        // of three spellings — `setXmlParam` (the canonical form),
        // `setXMLParam` (configtabs.m), and `setXMLparam` (menualign.m,
        // lowercase trailing param).  All point at the same body.
        {L"getXmlParam",             1, (void *)wq_getXmlParam},
        {L"setAlpha",                1, (void *)wq_setAlpha},
        {L"getAlpha",                0, (void *)wq_getAlpha},
        {L"getAutoWidth",            0, (void *)wq_getAutoWidth},
        {L"getAutoHeight",           0, (void *)wq_getAutoHeight},
        {L"getWidth",                0, (void *)wq_getWidth},
        {L"getHeight",               0, (void *)wq_getHeight},
        {L"getLeft",                 0, (void *)wq_getResolvedLeft},
        {L"getTop",                  0, (void *)wq_getResolvedTop},
        {L"getNumObjects",           0, (void *)wq_getNumObjects},
        {L"enumObject",              1, (void *)wq_enumObject},
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
        // Config / ConfigItem / ConfigAttribute / WinampConfig stubs.
        // All return a shared dummy ScriptObject so chained calls on
        // the result stay non-null.
        {L"newItem",                 2, (void *)wq_newItem},
        {L"getItem",                 1, (void *)wq_getItem},
        {L"getGroup",                1, (void *)wq_getItem},
        {L"newAttribute",            2, (void *)wq_newAttribute},
        {L"getAttribute",            1, (void *)wq_getAttribute},
        {L"setData",                 1, (void *)wq_setData},
        {L"getData",                 0, (void *)wq_getData},
        {L"onDataChanged",           0, (void *)wq_onDataChanged},
        {L"getInt",                  1, (void *)wq_getInt},
        {L"setEnabled",              1, (void *)wq_setEnabled},
        {L"getPosition",             0, (void *)wq_getPosition},
        {L"setPosition",             1, (void *)wq_setPosition},
        {L"getLayout",               1, (void *)wq_getLayoutByName},
        {L"getContainer",            1, (void *)wq_getContainer},
        {L"getCurContainer",         0, (void *)wq_getParentLayout},
        {L"findWac",                 1, (void *)wq_findWac},
        {L"newDynamicContainer",     1, (void *)wq_newDynamicContainer},
        {L"setFontSize",             1, (void *)wq_setFontSize},
        {L"getStatus",               0, (void *)wq_getStatus},
        // Layout target-animation (we apply immediately on
        // gotoTarget; setTargetSpeed is ignored).
        {L"setTargetX",              1, (void *)wq_setTargetX},
        {L"setTargetY",              1, (void *)wq_setTargetY},
        {L"setTargetW",              1, (void *)wq_setTargetW},
        {L"setTargetH",              1, (void *)wq_setTargetH},
        {L"setTargetSpeed",          1, (void *)wq_setTargetSpeed},
        {L"gotoTarget",              0, (void *)wq_gotoTarget},
        // GuiObject.resize(x,y,w,h).  Reuses setTarget+gotoTarget;
        // root layout → host resize, widget → its x/y/w/h.
        {L"resize",                  4, (void *)wq_resize},
        // Enabled-state + relative-sizing flag getters.  Symmetry
        // with setEnabled; relat getters back relat-conditional script
        // logic.  General: every skin that reads these now gets real values.
        {L"getEnabled",              0, (void *)wq_getEnabled},
        {L"getGuiRelatX",            0, (void *)wq_getGuiRelatX},
        {L"getGuiRelatY",            0, (void *)wq_getGuiRelatY},
        {L"getGuiRelatW",            0, (void *)wq_getGuiRelatW},
        {L"getGuiRelatH",            0, (void *)wq_getGuiRelatH},
        // Geometry getters that the drawer script reads back —
        // already bound for groups, but Layout shares the lookup.
        {L"getGuiW",                 0, (void *)wq_getGuiW},
        {L"getGuiH",                 0, (void *)wq_getGuiH},
        {L"getGuiX",                 0, (void *)wq_getLeft},
        {L"getGuiY",                 0, (void *)wq_getTop},
        // Feature-detection.  Bento's `tabcontrol.m` calls
        // System.hasVideoSupport() to decide whether to show the
        // Video tab.  qtamp does support video (QMediaPlayer has a
        // video sink path; we just don't ship a visualiser plugin
        // for AVS).  Return true so the tab strip matches reference.
        {L"hasVideoSupport",         0, (void *)wq_hasVideoSupport},
        // GuiObject.getId() — Bento's mainmenu.m derives the menu
        // index by tokenising the bound text layer's id (e.g. parses
        // "menu.text.file" → "file" → looks up "menu.layer.file.normal"
        // sibling).  Was missing → defaulted to int 0 → tokenisation
        // built wrong paths → menu background layers never positioned.
        {L"getId",                   0, (void *)wq_getId},
        // Layout scale + viewport queries.  Bento's
        // `simplemaximize.m` / `maximize.m` divide viewport sizes
        // by `getScale()` to derive the layout's natural pixel
        // dimensions; without bindings, getScale returned 0 →
        // division-by-zero Maki guru meditation → maximize/restore
        // buttons never fully initialised → titlebar half-rendered.
        {L"getScale",                       0, (void *)wq_getScale},
        {L"getViewPortWidthFromGuiObject",  1, (void *)wq_getViewPortWidthFromGuiObject},
        {L"getViewPortHeightFromGuiObject", 1, (void *)wq_getViewPortHeightFromGuiObject},
        {L"getViewPortLeftFromGuiObject",   1, (void *)wq_getViewPortLeftFromGuiObject},
        {L"getViewPortTopFromGuiObject",    1, (void *)wq_getViewPortTopFromGuiObject},
        // Lowercase variants — Bento's scripts use the canonical
        // mixed-case spellings above, but some skins use these
        // alternates.  Same bodies.
        // Named-window control (vis/video drawer detach in Winamp
        // Modern routes through these).
        {L"showWindow",                     3, (void *)wq_showWindow},
        {L"hideNamedWindow",                1, (void *)wq_hideNamedWindow},
        {L"isNamedWindowVisible",           1, (void *)wq_isNamedWindowVisible},
        // No-arg variants used by maximize.m's setWndToScreen.
        // Returning the layout's native dimensions keeps the
        // window at its canonical size on first launch instead of
        // ballooning to the viewer's display.
        {L"getViewportWidth",               0, (void *)wq_getViewportWidth},
        {L"getViewportHeight",              0, (void *)wq_getViewportHeight},
        {L"getViewportLeft",                0, (void *)wq_getViewportLeft},
        {L"getViewportTop",                 0, (void *)wq_getViewportTop},
    };
    if (count) *count = sizeof(kMethods) / sizeof(kMethods[0]);
    return kMethods;
}

// Case-insensitive flat lookup, shared with the class registry's
// migration fallback (maki-classes.cpp): a scoped row without an
// explicit pointer resolves its body — and its battle-tested arity —
// from here.
void *makiFlatLookup(const wchar_t *name, int *nparams) {
    if (!name) return nullptr;
    int n = 0;
    const MakiMethod *t = makiMethodTable(&n);
    for (int i = 0; i < n; ++i) {
        const wchar_t *a = t[i].name, *b = name;
        while (*a && *b) {
            wchar_t la = *a, lb = *b;
            if (la >= L'A' && la <= L'Z') la = wchar_t(la - L'A' + L'a');
            if (lb >= L'A' && lb <= L'Z') lb = wchar_t(lb - L'A' + L'a');
            if (la != lb) break;
            ++a; ++b;
        }
        if (*a == 0 && *b == 0) {
            if (nparams) *nparams = t[i].nparams;
            return t[i].ptr;
        }
    }
    return nullptr;
}

}  // namespace qtWasabi::Maki
