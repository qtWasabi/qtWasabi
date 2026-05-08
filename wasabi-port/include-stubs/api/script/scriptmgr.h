// Stub for upstream <api/script/scriptmgr.h>.  Declares SOM (and the
// per-script-object helpers) without dragging in the entire skin
// engine via systemobj/wacobj.
#ifndef _SCRIPT_H_WASABIQT_STUB
#define _SCRIPT_H_WASABIQT_STUB
#define _SCRIPT_H 1

#include <api/script/scriptobj.h>
#include <api/script/vcputypes.h>

#define SOM ScriptObjectManager

// SCRIPT_MAXARGS is normally pulled via objcontroller.h; vcpu.h's
// `paramList[SCRIPT_MAXARGS]` references it directly.
#ifndef SCRIPT_MAXARGS
#  define SCRIPT_MAXARGS 10
#endif

// MAKI_CMD_* — used by vcpu.cpp for the script-controller bridge.
// Real upstream pulls these via objcontroller.h.
#define MAKI_CMD_NONE     0
#define MAKI_CMD_SETDLF   1
#define MAKI_CMD_GETDLF   2
#define MAKI_CMD_ADDREF   3
#define MAKI_CMD_REMREF   4
#define MAKI_CMD_RESETDLF 5

// scriptVar factory used inside vcpu.cpp's debug paths.
scriptVar MAKE_SCRIPT_INT(int i);

// Forward — getSystemObjectByScriptId returns a SystemObject*, which
// has methods vcpu.cpp calls.  Pull the (also-stubbed) systemobj
// header for that.
#include <api/script/objects/systemobj.h>

class ScriptObjectManager {
public:
    ScriptObjectManager();
    ~ScriptObjectManager();

    static scriptVar makeVar(int type);
    static scriptVar makeVar(int type, ScriptObject *o);
    static void assign(scriptVar *v, const wchar_t *str);
    static void assign(scriptVar *v, int i);
    static void assign(scriptVar *v, float f);
    static void assign(scriptVar *v, double d);
    static void assign(scriptVar *v, ScriptObject *o);
    static void assign(scriptVar *v1, scriptVar *v2);
    static void assignPersistent(scriptVar *v1, scriptVar *v2);
    static void strflatassign(scriptVar *v, const wchar_t *str);
    static void persistentstrassign(scriptVar *v, const wchar_t *str);

    static int compEq (scriptVar *v1, scriptVar *v2);
    static int compNeq(scriptVar *v1, scriptVar *v2);
    static int compA  (scriptVar *v1, scriptVar *v2);
    static int compAe (scriptVar *v1, scriptVar *v2);
    static int compB  (scriptVar *v1, scriptVar *v2);
    static int compBe (scriptVar *v1, scriptVar *v2);

    static int    makeInt    (scriptVar *v);
    static float  makeFloat  (scriptVar *v);
    static double makeDouble (scriptVar *v);
    static bool   makeBoolean(scriptVar *v);
    static int    isNumeric  (scriptVar *s);
    static int    isString   (scriptVar *s);
    static int    isVoid     (scriptVar *s);
    static int    isObject   (scriptVar *s);
    static int    isNumericType(int t);

    static int typeCheck(VCPUscriptVar *v, int fail = 1);

    static SystemObject *getSystemObject(int scriptId);
    static SystemObject *getSystemObjectByScriptId(int scriptId);

    static void mid(wchar_t *dest, const wchar_t *str, int s, int l);
};

#endif
