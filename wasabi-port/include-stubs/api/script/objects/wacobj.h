// Stub for upstream <api/script/objects/wacobj.h>.  Pulled by
// scriptmgr.h chain — we never dispatch into Wac* from the VM layer
// during M2.
#pragma once
#define _WACOBJ_H 1
#define _WACOBJI_H 1

#include <api/script/scriptobj.h>

class WacObject : public ScriptObject {};
