// Stub overlay for upstream <Wasabi/precomp.h>.  Upstream's umbrella
// pulls api.h → 14 subsystem headers including api_threadpool.h whose
// first line is `#include <windows.h>` — unconditional, no platform
// dispatch.  We replace the umbrella with the bare set of subsystem
// includes the opensourced vcpu.cpp actually needs to parse.
#ifndef NULLSOFT_WASABI_PRECOMP_H
#define NULLSOFT_WASABI_PRECOMP_H
#define NULLSOFT_WASABI_API_H 1   // skip upstream api.h

// wasabicfg.h is force-overridden via wasabi-port-shim.h (pre-set guard).

// Minimum set of types vcpu.cpp transitively expects — declared by
// the headers below.
#include <bfc/wasabi_std.h>
#include <bfc/string/StringW.h>
#include <bfc/wasabi_std_rect.h>
#include <api/script/scriptobj.h>

#endif
