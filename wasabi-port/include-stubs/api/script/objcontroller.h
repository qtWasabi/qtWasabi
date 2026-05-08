// Stub overlay: vcpu.h needs SCRIPT_MAXARGS from this header.
// Upstream pulls api/service/svcs/svc_scriptobj.h (entire skin tree).
#pragma once
#define __SCRIPTOBJECTCONTROLLER_H 1
#define __SCRIPTOBJECTCONTROLLERI_H 1

#include <api/script/scriptobj.h>
#include <api/script/vcputypes.h>

#define SCRIPT_MAXARGS    10

#define MAKI_CMD_NONE     0
#define MAKI_CMD_SETDLF   1
#define MAKI_CMD_GETDLF   2
#define MAKI_CMD_ADDREF   3
#define MAKI_CMD_REMREF   4
#define MAKI_CMD_RESETDLF 5

class ScriptObjectController {
public:
    virtual ~ScriptObjectController() = default;
    virtual const wchar_t *getClassName() = 0;
};

class ScriptObjectControllerI : public ScriptObjectController {};
