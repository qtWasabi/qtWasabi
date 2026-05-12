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
#include <set>
#include <string>

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
int  currentScriptId() { return g_currentScript; }
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
    if (pub) {
        char pb[256];
        wq_wide_to_ascii(pub, pb, sizeof(pb));
        std::fprintf(stderr, " pub=%s", pb);
    }
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
    {L"setDelay",                1},
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

    // M14k: signatures merged from upstream ScriptObject getExportedFunctions tables.
    {L"CancelShutdown",                  0},
    {L"Chr",                             1},
    {L"GetApplicationName",              0},
    {L"GetApplicationPath",              0},
    {L"GetBuildNumber",                  0},
    {L"GetCommandLine",                  0},
    {L"GetExtension",                    1},
    {L"GetGUID",                         0},
    {L"GetMachineGUID",                  0},
    {L"GetPath",                         1},
    {L"GetSessionGUID",                  0},
    {L"GetSettingsPath",                 0},
    {L"GetUserGUID",                     0},
    {L"GetVersionNumberString",          0},
    {L"GetVersionString",                0},
    {L"GetWorkingPath",                  0},
    {L"IsShuttingDown",                  0},
    {L"RemovePath",                      1},
    {L"SetWorkingPath",                  1},
    {L"Shutdown",                        0},
    {L"StrLeft",                         2},
    {L"StrLen",                          1},
    {L"StrLower",                        1},
    {L"StrMid",                          3},
    {L"StrRight",                        2},
    {L"StrSearch",                       2},
    {L"StrUpper",                        1},
    {L"UrlDecode",                       1},
    {L"UrlEncode",                       1},
    {L"acos",                            1},
    {L"activateApplication",             0},
    {L"add",                             1},
    {L"addColumn",                       3},
    {L"addCommand",                      4},
    {L"addItem",                         1},
    {L"addSeparator",                    0},
    {L"addSubMenu",                      2},
    {L"addTreeItem",                     4},
    {L"apply",                           0},
    {L"asin",                            1},
    {L"atan",                            1},
    {L"atan2",                           2},
    {L"autoResize",                      0},
    {L"back",                            0},
    {L"beforeRedock",                    0},
    {L"bringAbove",                      1},
    {L"bringBelow",                      1},
    {L"callme",                          1},
    {L"cancelEditLabel",                 1},
    {L"cancelMenu",                      0},
    {L"cancelTarget",                    0},
    {L"center",                          0},
    {L"cfgGetFloat",                     0},
    {L"cfgGetGuid",                      0},
    {L"cfgGetInt",                       0},
    {L"cfgGetName",                      0},
    {L"cfgGetString",                    0},
    {L"cfgSetFloat",                     1},
    {L"cfgSetInt",                       1},
    {L"cfgSetString",                    1},
    {L"cfg_getAttributeName",            0},
    {L"cfg_getFloat",                    0},
    {L"cfg_getInt",                      0},
    {L"cfg_getItemGuid",                 0},
    {L"cfg_getString",                   0},
    {L"cfg_onDataChanged",               0},
    {L"cfg_setFloat",                    1},
    {L"cfg_setInt",                      1},
    {L"cfg_setString",                   1},
    {L"checkCommand",                    2},
    {L"clear",                           0},
    {L"clearPlaylist",                   0},
    {L"clientToScreenH",                 1},
    {L"clientToScreenW",                 1},
    {L"close",                           0},
    {L"closeList",                       0},
    {L"collapse",                        0},
    {L"collapseItem",                    1},
    {L"collapseItemDeferred",            1},
    {L"copy",                            1},
    {L"cos",                             1},
    {L"dateToLongTime",                  1},
    {L"dateToTime",                      1},
    {L"ddesend",                         3},
    {L"debugString",                     2},
    {L"delItem",                         1},
    {L"delItemDeferred",                 1},
    {L"delete",                          0},
    {L"deleteAllItems",                  0},
    {L"deleteByPos",                     1},
    {L"deselectAll",                     0},
    {L"disableCommand",                  2},
    {L"downloadMedia",                   4},
    {L"downloadURL",                     3},
    {L"editItemLabel",                   1},
    {L"editLabel",                       0},
    {L"eject",                           0},
    {L"end",                             0},
    {L"endModal",                        1},
    {L"enqueueFile",                     1},
    {L"ensureItemVisible",               1},
    {L"ensureVisible",                   0},
    {L"enter",                           0},
    {L"enumAllItems",                    1},
    {L"enumChildren",                    1},
    {L"enumColor",                       1},
    {L"enumContainer",                   1},
    {L"enumEmbedGUID",                   1},
    {L"enumGammaGroup",                  0},
    {L"enumGammaSet",                    1},
    {L"enumItem",                        1},
    {L"enumLayout",                      1},
    {L"enumObject",                      1},
    {L"enumRootItem",                    1},
    {L"enumVisibleChildItems",           2},
    {L"enumVisibleItems",                1},
    {L"exists",                          0},
    {L"expand",                          0},
    {L"expandItem",                      1},
    {L"expandItemDeferred",              1},
    {L"fake",                            0},
    {L"findItem",                        1},
    {L"findItem2",                       2},
    {L"findObjectXY",                    2},
    {L"floatToString",                   2},
    {L"formatDate",                      1},
    {L"formatLongDate",                  1},
    {L"forward",                         0},
    {L"frac",                            1},
    {L"freeCore",                        1},
    {L"freeCoreByName",                  1},
    {L"fx_getAlphaMode",                 0},
    {L"fx_getBgFx",                      0},
    {L"fx_getBilinear",                  0},
    {L"fx_getClear",                     0},
    {L"fx_getEnabled",                   0},
    {L"fx_getLocalized",                 0},
    {L"fx_getRealtime",                  0},
    {L"fx_getRect",                      0},
    {L"fx_getSpeed",                     0},
    {L"fx_getWrap",                      0},
    {L"fx_onFrame",                      0},
    {L"fx_onGetPixelA",                  4},
    {L"fx_onGetPixelD",                  4},
    {L"fx_onGetPixelR",                  4},
    {L"fx_onGetPixelX",                  4},
    {L"fx_onGetPixelY",                  4},
    {L"fx_onInit",                       0},
    {L"fx_restart",                      0},
    {L"fx_setAlphaMode",                 1},
    {L"fx_setBgFx",                      1},
    {L"fx_setBilinear",                  1},
    {L"fx_setClear",                     1},
    {L"fx_setEnabled",                   1},
    {L"fx_setGridSize",                  2},
    {L"fx_setLocalized",                 1},
    {L"fx_setRealtime",                  1},
    {L"fx_setRect",                      1},
    {L"fx_setSpeed",                     1},
    {L"fx_setWrap",                      1},
    {L"fx_update",                       0},
    {L"getARGBValue",                    3},
    {L"getActivated",                    0},
    {L"getActiveAlpha",                  0},
    {L"getAlbumArt",                     1},
    {L"getAtom",                         1},
    {L"getAttribute",                    1},
    {L"getAttributeName",                0},
    {L"getAutoEdit",                     0},
    {L"getAutoEnter",                    0},
    {L"getAutoReplay",                   0},
    {L"getBlue",                         0},
    {L"getBlueWithGamma",                0},
    {L"getBoost",                        0},
    {L"getBoundingBoxH",                 0},
    {L"getBoundingBoxW",                 0},
    {L"getBoundingBoxX",                 0},
    {L"getBoundingBoxY",                 0},
    {L"getBuildNumber",                  0},
    {L"getByLabel",                      2},
    {L"getChild",                        0},
    {L"getChildSibling",                 1},
    {L"getClassName",                    0},
    {L"getColor",                        1},
    {L"getColumnLabel",                  1},
    {L"getColumnNumeric",                1},
    {L"getColumnWidth",                  1},
    {L"getComponentName",                0},
    {L"getContainer",                    0},
    {L"getContent",                      0},
    {L"getContentsHeight",               0},
    {L"getContentsWidth",                0},
    {L"getCurAppHeight",                 0},
    {L"getCurAppLeft",                   0},
    {L"getCurAppTop",                    0},
    {L"getCurAppWidth",                  0},
    {L"getCurCfgVal",                    0},
    {L"getCurFrame",                     0},
    {L"getCurItem",                      0},
    {L"getCurLayout",                    0},
    {L"getCurPage",                      0},
    {L"getCurPlaybackNumber",            0},
    {L"getCurrent",                      0},
    {L"getCurrentGammaSet",              0},
    {L"getCurrentIndex",                 0},
    {L"getCurrentTrackRating",           0},
    {L"getCustomText",                   0},
    {L"getData",                         0},
    {L"getDateDay",                      1},
    {L"getDateDow",                      1},
    {L"getDateDoy",                      1},
    {L"getDateDst",                      1},
    {L"getDateHour",                     1},
    {L"getDateMin",                      1},
    {L"getDateMonth",                    1},
    {L"getDateSec",                      1},
    {L"getDateYear",                     1},
    {L"getDecoderName",                  1},
    {L"getDelay",                        0},
    {L"getDesktopAlpha",                 0},
    {L"getDirection",                    0},
    {L"getDocumentTitle",                0},
    {L"getDownloadPath",                 0},
    {L"getEmbeddedObject",               0},
    {L"getEnabled",                      0},
    {L"getEndFrame",                     0},
    {L"getEq",                           0},
    {L"getEqAuto",                       0},
    {L"getEqBand",                       1},
    {L"getEqPreAmp",                     0},
    {L"getEqPreamp",                     0},
    {L"getEqStatus",                     0},
    {L"getExtFamily",                    1},
    {L"getFileName",                     1},
    {L"getFileSize",                     1},
    {L"getFirstItemSelected",            0},
    {L"getFirstItemVisible",             0},
    {L"getFontSize",                     0},
    {L"getGUID",                         1},
    {L"getGammaGroup",                   1},
    {L"getGammaSet",                     1},
    {L"getGammagroup",                   0},
    {L"getGeneralGroup",                 0},
    {L"getGray",                         0},
    {L"getGreen",                        0},
    {L"getGreenWithGamma",               0},
    {L"getGuiH",                         0},
    {L"getGuiRelatH",                    0},
    {L"getGuiRelatW",                    0},
    {L"getGuiRelatX",                    0},
    {L"getGuiRelatY",                    0},
    {L"getGuiW",                         0},
    {L"getGuiX",                         0},
    {L"getGuiY",                         0},
    {L"getGuid",                         0},
    {L"getHeaderHeight",                 0},
    {L"getID",                           0},
    {L"getIconHeight",                   0},
    {L"getIconWidth",                    0},
    {L"getId",                           0},
    {L"getIdealVideoHeight",             0},
    {L"getIdealVideoWidth",              0},
    {L"getIdleEnabled",                  0},
    {L"getInactiveAlpha",                0},
    {L"getInterface",                    1},
    {L"getItem",                         1},
    {L"getItemByGuid",                   1},
    {L"getItemCount",                    0},
    {L"getItemFocused",                  0},
    {L"getItemFromPoint",                2},
    {L"getItemIcon",                     1},
    {L"getItemLabel",                    2},
    {L"getItemRectH",                    1},
    {L"getItemRectW",                    1},
    {L"getItemRectX",                    1},
    {L"getItemRectY",                    1},
    {L"getItemSelected",                 1},
    {L"getItemText",                     1},
    {L"getLabel",                        0},
    {L"getLanguageId",                   0},
    {L"getLastAddedItemPos",             0},
    {L"getLastItemVisible",              0},
    {L"getLayout",                       1},
    {L"getLeftVuMeter",                  0},
    {L"getLength",                       0},
    {L"getMainBrowser",                  0},
    {L"getMaxHeight",                    0},
    {L"getMaxWidth",                     0},
    {L"getMenu",                         0},
    {L"getMenuGroup",                    0},
    {L"getMetaData",                     2},
    {L"getMetadataString",               2},
    {L"getMode",                         0},
    {L"getMonitorHeight",                0},
    {L"getMonitorHeightFromGuiObject",   1},
    {L"getMonitorHeightFromPoint",       2},
    {L"getMonitorLeft",                  0},
    {L"getMonitorLeftFromGuiObject",     1},
    {L"getMonitorLeftFromPoint",         2},
    {L"getMonitorTop",                   0},
    {L"getMonitorTopFromGuiObject",      1},
    {L"getMonitorTopFromPoint",          2},
    {L"getMonitorWidth",                 0},
    {L"getMonitorWidthFromGuiObject",    1},
    {L"getMonitorWidthFromPoint",        2},
    {L"getMousePosX",                    0},
    {L"getMousePosY",                    0},
    {L"getMute",                         0},
    {L"getName",                         0},
    {L"getNamedCore",                    1},
    {L"getNextItemSelected",             1},
    {L"getNextSelectedTrack",            1},
    {L"getNthChild",                     1},
    {L"getNumChildren",                  0},
    {L"getNumColors",                    0},
    {L"getNumColumns",                   0},
    {L"getNumCommands",                  0},
    {L"getNumContainers",                0},
    {L"getNumGammaGroups",               0},
    {L"getNumGammaSets",                 0},
    {L"getNumItems",                     0},
    {L"getNumLayouts",                   0},
    {L"getNumObjects",                   0},
    {L"getNumRootItems",                 0},
    {L"getNumSelectedTracks",            0},
    {L"getNumTracks",                    0},
    {L"getNumVisibleChildItems",         1},
    {L"getNumVisibleItems",              0},
    {L"getPan",                          0},
    {L"getParentItem",                   0},
    {L"getPath",                         0},
    {L"getPlayItemLength",               0},
    {L"getPlayItemMetadataString",       1},
    {L"getPlayItemString",               0},
    {L"getPlaylistIndex",                0},
    {L"getPlaylistLength",               0},
    {L"getPosition",                     0},
    {L"getPreventMultipleSelection",     0},
    {L"getPriority",                     0},
    {L"getPrivateString",                3},
    {L"getPublicString",                 2},
    {L"getRating",                       1},
    {L"getRealtime",                     0},
    {L"getRed",                          0},
    {L"getRedWithGamma",                 0},
    {L"getRedirection",                  0},
    {L"getRegion",                       0},
    {L"getRightVuMeter",                 0},
    {L"getScale",                        0},
    {L"getScroll",                       0},
    {L"getSelected",                     0},
    {L"getSelectedText",                 0},
    {L"getShowIcons",                    0},
    {L"getSibling",                      0},
    {L"getSize",                         0},
    {L"getSkipped",                      0},
    {L"getSnapAdjustBottom",             0},
    {L"getSnapAdjustLeft",               0},
    {L"getSnapAdjustRight",              0},
    {L"getSnapAdjustTop",                0},
    {L"getSongInfoText",                 0},
    {L"getSongInfoTextTranslated",       0},
    {L"getSortColumn",                   0},
    {L"getSortDirection",                0},
    {L"getSorted",                       0},
    {L"getStartFrame",                   0},
    {L"getStatus",                       0},
    {L"getStatusBar",                    0},
    {L"getString",                       2},
    {L"getStuff",                        0},
    {L"getSubitemText",                  2},
    {L"getTextWidth",                    0},
    {L"getTitle",                        1},
    {L"getTopParent",                    0},
    {L"getTree",                         0},
    {L"getValue",                        2},
    {L"getViewportHeight",               0},
    {L"getViewportHeightFromGuiObject",  1},
    {L"getViewportHeightFromPoint",      2},
    {L"getViewportLeft",                 0},
    {L"getViewportLeftFromGuiObject",    1},
    {L"getViewportLeftFromPoint",        2},
    {L"getViewportTop",                  0},
    {L"getViewportTopFromGuiObject",     1},
    {L"getViewportTopFromPoint",         2},
    {L"getViewportWidth",                0},
    {L"getViewportWidthFromGuiObject",   1},
    {L"getViewportWidthFromPoint",       2},
    {L"getVisBand",                      2},
    {L"getVolume",                       0},
    {L"getWac",                          1},
    {L"getWantAutoDeselect",             0},
    {L"getWinampVersion",                0},
    {L"getWritePosition",                0},
    {L"gotoFrame",                       1},
    {L"gotoTarget",                      0},
    {L"gotoUrl",                         1},
    {L"hasSubItems",                     0},
    {L"hasVideoSupport",                 0},
    {L"hideNamedWindow",                 1},
    {L"hideWindow",                      1},
    {L"hiliteItem",                      1},
    {L"hitTest",                         2},
    {L"home",                            0},
    {L"inRegion",                        2},
    {L"init",                            1},
    {L"insertItem",                      2},
    {L"instantiate",                     2},
    {L"integer",                         1},
    {L"integerToLongTime",               1},
    {L"integerToTime",                   1},
    {L"invalidate",                      0},
    {L"invalidateColumns",               0},
    {L"invalidateItem",                  1},
    {L"invertSelection",                 0},
    {L"invokeDebugger",                  0},
    {L"isActive",                        0},
    {L"isAppActive",                     0},
    {L"isChecked",                       0},
    {L"isCollapsed",                     0},
    {L"isColumnDynamic",                 1},
    {L"isDesktopAlphaAvailable",         0},
    {L"isDynamic",                       0},
    {L"isExpanded",                      0},
    {L"isGoingToTarget",                 0},
    {L"isHilited",                       0},
    {L"isInvalid",                       0},
    {L"isItemFocused",                   1},
    {L"isKeyDown",                       1},
    {L"isLayout",                        0},
    {L"isLayoutAnimationSafe",           0},
    {L"isLoadingSkin",                   0},
    {L"isMinimized",                     0},
    {L"isMouseOver",                     2},
    {L"isMouseOverRect",                 0},
    {L"isNamedWindowVisible",            1},
    {L"isObjectValid",                   1},
    {L"isPaused",                        0},
    {L"isPlaying",                       0},
    {L"isProVersion",                    0},
    {L"isRunning",                       0},
    {L"isSelected",                      0},
    {L"isSorted",                        0},
    {L"isStopped",                       0},
    {L"isTransparencySafe",              0},
    {L"isVideo",                         0},
    {L"isVideoFullscreen",               0},
    {L"isVisible",                       0},
    {L"jumpToNext",                      1},
    {L"leftClick",                       0},
    {L"ln",                              1},
    {L"load",                            1},
    {L"loadFromBitmap",                  1},
    {L"loadFromMap",                     3},
    {L"loadMap",                         1},
    {L"lock",                            0},
    {L"lockUI",                          0},
    {L"log10",                           1},
    {L"messageToJS",                     5},
    {L"messageToMaki",                   5},
    {L"minimizeApplication",             0},
    {L"moveDown",                        1},
    {L"moveItem",                        2},
    {L"moveTo",                          2},
    {L"moveTreeItem",                    2},
    {L"moveUp",                          1},
    {L"navigateUrl",                     1},
    {L"newAttribute",                    2},
    {L"newDynamicContainer",             1},
    {L"newGammaSet",                     1},
    {L"newGroup",                        1},
    {L"newGroupAsLayout",                1},
    {L"newItem",                         2},
    {L"newNamedCore",                    1},
    {L"next",                            0},
    {L"nextMenu",                        0},
    {L"nextMode",                        0},
    {L"notify",                          4},
    {L"offset",                          2},
    {L"onAbort",                         0},
    {L"onAbortCurrentSong",              0},
    {L"onAccelerator",                   3},
    {L"onAction",                        7},
    {L"onActivate",                      1},
    {L"onAddContent",                    3},
    {L"onBeforeLoadingElements",         0},
    {L"onBeforeNavigate",                3},
    {L"onBeforeSwitchToLayout",          2},
    {L"onBeginLabelEdit",                0},
    {L"onBeginResize",                   4},
    {L"onCfgChanged",                    0},
    {L"onChar",                          1},
    {L"onCloseMenu",                     0},
    {L"onCollapse",                      0},
    {L"onColorThemeChanged",             1},
    {L"onColorThemesListChanged",        0},
    {L"onColumnDblClick",                3},
    {L"onColumnLabelClick",              3},
    {L"onContextMenu",                   2},
    {L"onConvertersChainRebuilt",        0},
    {L"onCoreStatusMsg",                 1},
    {L"onCreateLayout",                  1},
    {L"onCreateObject",                  1},
    {L"onDataChanged",                   0},
    {L"onDelete",                        0},
    {L"onDeselect",                      0},
    {L"onDock",                          1},
    {L"onDocumentComplete",              1},
    {L"onDocumentReady",                 1},
    {L"onDoubleClick",                   1},
    {L"onDownloadFinished",              3},
    {L"onDragEnter",                     0},
    {L"onDragLeave",                     0},
    {L"onDragOver",                      2},
    {L"onEQAutoChange",                  1},
    {L"onEQBandChange",                  2},
    {L"onEQFreqChange",                  1},
    {L"onEQPreampChange",                1},
    {L"onEQStatusChange",                1},
    {L"onEditUpdate",                    0},
    {L"onEnable",                        1},
    {L"onEndLabelEdit",                  1},
    {L"onEndMove",                       0},
    {L"onEndOfDecode",                   0},
    {L"onEndResize",                     4},
    {L"onEnter",                         0},
    {L"onEnterArea",                     0},
    {L"onEqBandChanged",                 2},
    {L"onEqChanged",                     1},
    {L"onEqFreqChanged",                 1},
    {L"onEqPreAmpChanged",               1},
    {L"onErrorMsg",                      1},
    {L"onErrorOccured",                  2},
    {L"onExpand",                        0},
    {L"onFeedChange",                    1},
    {L"onFileComplete",                  1},
    {L"onFrame",                         0},
    {L"onGetCancelComponent",            2},
    {L"onGetFocus",                      0},
    {L"onGuiLoaded",                     0},
    {L"onHide",                          0},
    {L"onHideLayout",                    1},
    {L"onIconLeftclick",                 3},
    {L"onIdleEditUpdate",                0},
    {L"onInfoChange",                    1},
    {L"onItemDeselected",                1},
    {L"onItemRecvDrop",                  1},
    {L"onItemSelected",                  1},
    {L"onItemSelection",                 2},
    {L"onKeyDown",                       1},
    {L"onKeyUp",                         1},
    {L"onKillFocus",                     0},
    {L"onLabelChange",                   1},
    {L"onLeaveArea",                     0},
    {L"onLeftButtonDblClk",              2},
    {L"onLeftButtonDown",                2},
    {L"onLeftButtonUp",                  2},
    {L"onLeftDoubleClick",               0},
    {L"onLengthChange",                  1},
    {L"onLinksUpdated",                  1},
    {L"onLoaded",                        0},
    {L"onLookForComponent",              1},
    {L"onMediaFamilyChange",             1},
    {L"onMediaLink",                     1},
    {L"onMouseEnterLayout",              0},
    {L"onMouseLeaveLayout",              0},
    {L"onMouseMove",                     2},
    {L"onMouseWheelDown",                2},
    {L"onMouseWheelUp",                  2},
    {L"onMove",                          0},
    {L"onNavigateError",                 2},
    {L"onNeedNextFile",                  1},
    {L"onNextFile",                      0},
    {L"onOpenMenu",                      0},
    {L"onOpenURL",                       1},
    {L"onPanChange",                     1},
    {L"onPathChanged",                   1},
    {L"onPaused",                        0},
    {L"onPleditModified",                0},
    {L"onPostedPosition",                1},
    {L"onQuit",                          0},
    {L"onResume",                        0},
    {L"onRightButtonDblClk",             2},
    {L"onRightButtonDown",               2},
    {L"onRightButtonUp",                 2},
    {L"onRightClick",                    1},
    {L"onRightDoubleClick",              0},
    {L"onScale",                         1},
    {L"onSecondLeftClick",               1},
    {L"onSeek",                          1},
    {L"onSeeked",                        1},
    {L"onSelect",                        0},
    {L"onSelectAll",                     0},
    {L"onSetFinalPosition",              1},
    {L"onSetNextFile",                   1},
    {L"onSetPosition",                   1},
    {L"onSetVisible",                    1},
    {L"onShow",                          0},
    {L"onShowLayout",                    1},
    {L"onShowNotification",              0},
    {L"onSnapAdjustChanged",             0},
    {L"onStarted",                       0},
    {L"onStartup",                       0},
    {L"onStatusMsg",                     1},
    {L"onStopped",                       0},
    {L"onSwitchToLayout",                1},
    {L"onTargetReached",                 0},
    {L"onToggle",                        1},
    {L"onTreeAdd",                       0},
    {L"onTreeRemove",                    0},
    {L"onUndock",                        0},
    {L"onUnpaused",                      0},
    {L"onUrlChange",                     1},
    {L"onUserResize",                    4},
    {L"onViewPortChanged",               2},
    {L"onVolumeChange",                  1},
    {L"onVolumeChanged",                 1},
    {L"onWantAutoContextMenu",           0},
    {L"onWarningMsg",                    1},
    {L"openList",                        0},
    {L"pagedown",                        0},
    {L"pageup",                          0},
    {L"parser_addCallback",              1},
    {L"parser_destroy",                  0},
    {L"parser_onCallback",               4},
    {L"parser_onCloseCallback",          2},
    {L"parser_onError",                  5},
    {L"parser_start",                    0},
    {L"pause",                           0},
    {L"play",                            0},
    {L"playFile",                        1},
    {L"playTrack",                       1},
    {L"popAtMouse",                      0},
    {L"popAtXY",                         2},
    {L"popMainBrowser",                  0},
    {L"popParentLayout",                 0},
    {L"pow",                             2},
    {L"previous",                        0},
    {L"previousMenu",                    0},
    {L"random",                          1},
    {L"rebuildConvertersChain",          0},
    {L"redock",                          0},
    {L"refresh",                         0},
    {L"releaseFeed",                     0},
    {L"remove",                          0},
    {L"removeAll",                       0},
    {L"removeItem",                      1},
    {L"removeTrack",                     1},
    {L"removeTreeItem",                  1},
    {L"rename",                          1},
    {L"reset",                           0},
    {L"resize",                          4},
    {L"resort",                          0},
    {L"restoreApplication",              0},
    {L"reverseTarget",                   1},
    {L"rightClick",                      0},
    {L"runModal",                        0},
    {L"scrape",                          0},
    {L"screenToClientH",                 1},
    {L"screenToClientW",                 1},
    {L"scrollAbsolute",                  1},
    {L"scrollDown",                      1},
    {L"scrollLeft",                      1},
    {L"scrollRelative",                  1},
    {L"scrollRight",                     1},
    {L"scrollToItem",                    1},
    {L"scrollToPercent",                 1},
    {L"scrollUp",                        1},
    {L"seekTo",                          1},
    {L"selectAll",                       0},
    {L"selectCurrent",                   0},
    {L"selectFile",                      3},
    {L"selectFirstEntry",                0},
    {L"selectFolder",                    3},
    {L"selectItem",                      1},
    {L"selectItemDeferred",              1},
    {L"sendAction",                      6},
    {L"sendCommand",                     4},
    {L"setActivated",                    1},
    {L"setActivatedNoCallback",          1},
    {L"setActiveAlpha",                  1},
    {L"setAlternateText",                1},
    {L"setAtom",                         2},
    {L"setAutoCollapse",                 1},
    {L"setAutoEdit",                     1},
    {L"setAutoEnter",                    1},
    {L"setAutoReplay",                   1},
    {L"setAutoSort",                     1},
    {L"setBlue",                         1},
    {L"setBoost",                        1},
    {L"setCancelIEErrorPage",            1},
    {L"setChecked",                      1},
    {L"setChildTab",                     1},
    {L"setClipboardText",                1},
    {L"setColumnDynamic",                2},
    {L"setColumnLabel",                  2},
    {L"setColumnWidth",                  2},
    {L"setCurPage",                      1},
    {L"setCurrentTrackRating",           1},
    {L"setCustomMsg",                    1},
    {L"setData",                         1},
    {L"setDesktopAlpha",                 1},
    {L"setDownId",                       1},
    {L"setDownloadPath",                 1},
    {L"setEnabled",                      1},
    {L"setEndFrame",                     1},
    {L"setEq",                           1},
    {L"setEqAuto",                       1},
    {L"setEqBand",                       2},
    {L"setEqPreAmp",                     1},
    {L"setEqPreamp",                     1},
    {L"setEqStatus",                     1},
    {L"setFeed",                         1},
    {L"setFocus",                        0},
    {L"setFontSize",                     1},
    {L"setGray",                         1},
    {L"setGreen",                        1},
    {L"setHilited",                      1},
    {L"setHoverId",                      1},
    {L"setID",                           1},
    {L"setIconHeight",                   1},
    {L"setIconWidth",                    1},
    {L"setIdleEnabled",                  1},
    {L"setInactiveAlpha",                1},
    {L"setItem",                         2},
    {L"setItemFocused",                  1},
    {L"setItemIcon",                     2},
    {L"setItemLabel",                    2},
    {L"setItems",                        1},
    {L"setLabel",                        1},
    {L"setListHeight",                   1},
    {L"setMenu",                         1},
    {L"setMenuGroup",                    1},
    {L"setMenuTransparency",             1},
    {L"setMinimumSize",                  1},
    {L"setMode",                         1},
    {L"setMute",                         1},
    {L"setName",                         1},
    {L"setNextFile",                     1},
    {L"setNoItemText",                   1},
    {L"setNormalId",                     1},
    {L"setPan",                          1},
    {L"setPosition",                     1},
    {L"setPreventMultipleSelection",     1},
    {L"setPriority",                     1},
    {L"setPrivateString",                3},
    {L"setPublicString",                 2},
    {L"setRating",                       2},
    {L"setRealtime",                     1},
    {L"setRed",                          1},
    {L"setRedirection",                  1},
    {L"setRedraw",                       1},
    {L"setRedrawOnResize",               1},
    {L"setRegion",                       1},
    {L"setRegionFromMap",                3},
    {L"setScale",                        1},
    {L"setScroll",                       1},
    {L"setSelected",                     2},
    {L"setSelectionEnd",                 1},
    {L"setSelectionStart",               1},
    {L"setShowIcons",                    1},
    {L"setSize",                         1},
    {L"setSortColumn",                   1},
    {L"setSortDirection",                1},
    {L"setSorted",                       1},
    {L"setSpeed",                        1},
    {L"setStartFrame",                   1},
    {L"setStatusBar",                    1},
    {L"setStatusText",                   2},
    {L"setSubItem",                      3},
    {L"setTargetA",                      1},
    {L"setTargetH",                      1},
    {L"setTargetName",                   1},
    {L"setTargetSpeed",                  1},
    {L"setTargetW",                      1},
    {L"setTargetX",                      1},
    {L"setTargetY",                      1},
    {L"setVideoFullscreen",              1},
    {L"setVolume",                       1},
    {L"setWantAutoDeselect",             1},
    {L"showCurrentlyPlayingTrack",       0},
    {L"showTrack",                       1},
    {L"showWindow",                      3},
    {L"sin",                             1},
    {L"snapAdjust",                      4},
    {L"sortTreeItems",                   0},
    {L"spawnMenu",                       1},
    {L"sqr",                             1},
    {L"sqrt",                            1},
    {L"start",                           0},
    {L"stretch",                         1},
    {L"stringToFloat",                   1},
    {L"sub",                             1},
    {L"swapTracks",                      2},
    {L"switchSkin",                      1},
    {L"switchToLayout",                  1},
    {L"systemMenu",                      0},
    {L"tan",                             1},
    {L"toggle",                          0},
    {L"togglePause",                     0},
    {L"toggleSelection",                 2},
    {L"translate",                       1},
    {L"triggerAction",                   3},
    {L"unhiliteItem",                    1},
    {L"unlock",                          0},
    {L"unlockUI",                        0},
    {L"updateLinks",                     2},
    {L"userButton",                      1},
    {L"windowMenu",                      0},
    {nullptr, 0},
};

// Method names in .maki bytecode are stored verbatim from whatever
// casing the compiler emitted, and different skins/scripts use
// different conventions (setalpha vs setAlpha vs SetAlpha). Match
// case-insensitively so we resolve them all to the same nparams.
static int wq_wcsicmp(const wchar_t *a, const wchar_t *b) {
    while (*a && *b) {
        wchar_t la = *a, lb = *b;
        if (la >= L'A' && la <= L'Z') la = wchar_t(la - L'A' + L'a');
        if (lb >= L'A' && lb <= L'Z') lb = wchar_t(lb - L'A' + L'a');
        if (la != lb) return int(la) - int(lb);
        ++a; ++b;
    }
    return int(*a) - int(*b);
}

int lookupNparams(const wchar_t *name) {
    if (!name) return 0;
    for (auto *m = kKnownMethods; m->name; ++m)
        if (wq_wcsicmp(m->name, name) == 0) return m->nparams;
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
            if (wq_wcsicmp(t[i].name, dlf->functionName) == 0) {
                dlf->nparams = t[i].nparams;
                dlf->ptr     = t[i].ptr;
                return 1;
            }
        }
        // Known by name only → safe no-op via nparams (e->ptr stays
        // NULL; CALLM short-circuits to int 0 with stack still aligned).
        // Trace fallthroughs to identify methods missing from both
        // tables so we can grow them. Set WASABIQT_TRACE_UNKNOWN_DLF=1
        // to log each new method seen exactly once.
        dlf->nparams = lookupNparams(dlf->functionName);
        dlf->ptr     = nullptr;
        if (const char *t2 = ::getenv("WASABIQT_TRACE_UNKNOWN_DLF");
            t2 && *t2 == '1') {
            // Cheap dedupe via a static hash; the table is bounded by
            // the number of distinct method names a skin can call.
            static std::set<std::wstring> seen;
            std::wstring k(dlf->functionName);
            if (seen.insert(k).second) {
                char nb[128];
                wq_wide_to_ascii(dlf->functionName, nb, sizeof(nb));
                std::fprintf(stderr,
                    "[unknown-dlf] %s nparams=%d (defaulted from lookupNparams)\n",
                    nb, dlf->nparams);
            }
        }
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
// VCPUassign uses this to gate object-typed assignment: when it
// returns 0 the assigned pointer gets nullified before the slot
// receives it, which kills every `local = Config.newItem(...)` and
// similar chains.  Upstream's real implementation walks the live
// ScriptObject registry; we don't track that here yet — but every
// ScriptObject we hand to the VM is genuinely live (we never free
// them until SkinRuntime::destroyAll, and assignment can only happen
// during script dispatch, which can't outlive that).  Treat all
// non-null receivers as valid.
int SystemObject::isObjectValid(ScriptObject *o)            { return o != nullptr; }
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
