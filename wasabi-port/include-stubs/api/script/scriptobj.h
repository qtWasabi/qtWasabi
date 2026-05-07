// Stub overlay for upstream <api/script/scriptobj.h>.  Upstream pulls
// the full Wasabi/Dispatchable hierarchy and tons of widget headers;
// we provide only the abstract surface vcpu.cpp uses to dispatch
// methods on script-visible objects.
#ifndef __SCRIPTOBJ_H_WASABIQT_STUB
#define __SCRIPTOBJ_H_WASABIQT_STUB
#define _SCRIPTOBJI_H 1

#include <api/script/vcputypes.h>

class ScriptObject {
public:
    virtual ~ScriptObject() = default;

    // Real upstream signatures — vcpu.cpp dispatches against these.
    virtual void *vcpu_getInterfaceObject(GUID g, ScriptObject **o);
    virtual int   vcpu_getAssignedVariable(int start, int scriptid,
                                           int functionId, int *next,
                                           int *globalevententry,
                                           int *inheritedevent);
    virtual void  vcpu_addAssignedVariable(int var, int scriptid);
    virtual void  vcpu_removeAssignedVariable(int var, int id);
    virtual void  vcpu_setScriptId(int i);
    virtual void  vcpu_delMembers(int scriptid);
    virtual int   vcpu_getMember(const wchar_t *id, int scriptid, int rettype);
    virtual ScriptObject *getScriptObject() { return this; }
};

class ScriptObjectI : public ScriptObject {};

#endif
