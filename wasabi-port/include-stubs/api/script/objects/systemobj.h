// Stub for upstream <api/script/objects/systemobj.h>.  Provides only
// the SystemObject static + instance method surface vcpu.cpp uses.
#pragma once
#define _SYSTEMOBJ_H 1
#define __SYSTEMOBJI_H 1
#define _SYSTEMOBJX_H 1

#include <api/script/scriptobj.h>
#include <bfc/ptrlist.h>
#include <bfc/tlist.h>

class SystemObject : public ScriptObject {
public:
    static int                   isObjectValid(ScriptObject *o);
    static PtrList<ScriptObject> *getAllScriptObjects();

    // Per-instance methods called by vcpu.cpp on objects returned by
    // SOM::getSystemObjectByScriptId().
    TList<int> *getTypesList();
    void        setIsOldFormat(int isOld);
    int         isOldFormat();
    int         isLoaded();
    void        addInstantiatedObject(ScriptObject *obj);
    void        removeInstantiatedObject(ScriptObject *obj);

    // Called when a script is unloaded.
    virtual void onUnload();
};
