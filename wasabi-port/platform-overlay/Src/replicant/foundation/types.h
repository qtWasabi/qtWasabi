#pragma once
// Platform dispatch for replicant/foundation types (qtWasabi overlay).
//
// Mirrors the upstream cascade and adds the targets upstream never had:
// wasm32 (Emscripten) for the in-browser player.  Copied over the
// user-supplied tree by the platform-overlay step; behaviour on the
// original platforms is unchanged.
#if defined(_WIN64) && defined(_M_X64)
#include "win-amd64/types.h"
#elif defined(_WIN32) && defined(_M_IX86)
#include "win-x86/types.h"
#elif defined(__EMSCRIPTEN__)
#include "wasm32/types.h"
#elif defined(__APPLE__) && defined(__LP64__)
#include "osx-amd64/types.h"
#elif defined(__APPLE__)
#include "osx-x86/types.h"
#elif defined(__ANDROID__)
#include "android-arm/types.h"
#elif defined(__linux__) && defined(__x86_64)
#include "linux-amd64/types.h"
#elif defined(__linux__)
// Non-amd64 Linux (aarch64 etc.): the linux-amd64 typedefs carry no
// pointer-width assumptions, so they serve every Linux ABI we build.
#include "linux-amd64/types.h"
#else
#error port me!
#endif
