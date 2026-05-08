// Stub overlay for upstream <api/script/script.h>.  Replicates the
// constants and `Script` declaration vcpu.cpp uses, without pulling
// in the skin/widget tree.
#ifndef __SCRIPT_H_WASABIQT_STUB
#define __SCRIPT_H_WASABIQT_STUB
#define __SCRIPT_H 1

#include <bfc/ptrlist.h>
#include <api/script/vcputypes.h>

class String;

#define GURU_POPEMPTYSTACK      0
#define GURU_INVALIDHEADER      1
#define GURU_INVALIDFUNCINDLF   2
#define GURU_INVALIDFUNCBT      3
#define GURU_INVALIDVARBT       4
#define GURU_INVALIDEVENTDLF    5
#define GURU_INVALIDEVENTADDR   6
#define GURU_INVALIDEVENTVAR    7
#define GURU_INVALIDSCRIPTID    8
#define GURU_DLFSETUPFAILED     9
#define GURU_SETNONINTERNAL    10
#define GURU_INCSNONNUM        11
#define GURU_DECSNONNUM        12
#define GURU_INCPNONNUM        13
#define GURU_DECPNONNUM        14
#define GURU_OBJECTADD         15
#define GURU_SUBNONNUM         16
#define GURU_MULNONNUM         17
#define GURU_DIVNONNUM         18
#define GURU_DIVBYZERO         19
#define GURU_MODNONNUM         20
#define GURU_NEGNONNUM         21
#define GURU_BNOTNONNUM        22
#define GURU_SHLNONNUM         23
#define GURU_SHRNONNUM         24
#define GURU_XORNONNUM         25
#define GURU_ANDNONNUM         26
#define GURU_NEWFAILED         27
#define GURU_NULLCALLED        28
#define GURU_OLDFORMAT         29
#define GURU_INVALIDPEEKSTACK  30
#define GURU_INVALIDOLDID      31
#define GURU_INCOMPATIBLEOBJECT 32
#define GURU_EXCEPTION         33
#define GURU_FUTUREFORMAT      34

class SystemObject;

class Script {
public:
    static void guruMeditation(SystemObject *script, int code,
                               const wchar_t *pub = nullptr,
                               int intinfo = 0);
};

#endif
