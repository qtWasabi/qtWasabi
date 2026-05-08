// Stub overlay for upstream <api/script/objecttable.h>.  Upstream
// pulls every widget header to register them by GUID.  We declare
// only the static method surface vcpu.cpp calls; the WasabiQT layer
// implements them and registers WasabiQt's own widget classes.
#pragma once
#define _OBJECTTABLE_H 1
#define __OBJECTTABLEI_H 1

#include <api/script/scriptobj.h>
#include <api/script/vcputypes.h>
#include <bfc/ptrlist.h>
#include <bfc/nsguid.h>

#define CLASS_ID_BASE 0x100

// Members vcpu.cpp accesses on class_entry (e->classGuid, e->classname).
class waServiceFactory;
class ScriptObjectController;

class class_entry {
public:
    const wchar_t *classname;
    int            classid;
    int            ancestorclassid;
    ScriptObjectController *controller;
    GUID           classGuid;
    int            instantiable;
    int            referenceable;
    int            external;
    waServiceFactory *sf;
};

class ObjectTable {
public:
    static int            addrefDLF(VCPUdlfEntry *dlf, int id);
    static void           delrefDLF(VCPUdlfEntry *dlf);
    static int            getClassFromName(const wchar_t *className);
    static int            getClassFromGuid(GUID g);
    static const wchar_t *getClassName(int classid);
    static int            getClassEntryIdx(int classid);
    static int            isClassInstantiable(int classid);
    static int            isClassReferenceable(int classid);
    static ScriptObject  *instantiate(int classid);
    static void           destroy(ScriptObject *o);
    static class_entry   *getClassEntry(int classid);
};
