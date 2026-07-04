#ifndef NULLSOFT_BFC_PLATFORM_MINMAX_H
#define NULLSOFT_BFC_PLATFORM_MINMAX_H
// qtWasabi include-stubs overlay of bfc/platform/minmax.h.
//
// The upstream header only defines the MIN/MAX macros for _WIN32,
// __APPLE__ and __linux__, so translation units that include it (for
// example bfc/wasabi_std_rect.cpp) get no MIN/MAX under Emscripten.
// This overlay wins the include-path race (include-stubs precedes
// wasabi-src) and reproduces the upstream behaviour with the wasm
// target added.  Deliberately NOT a global define: bfc/std_math.h
// declares MIN/MAX as templates, and a force-included macro would
// destroy those declarations, exactly as on the native platforms.
#if (defined(_WIN32) || defined(__APPLE__) || defined(__linux__) || \
     defined(__EMSCRIPTEN__)) && !defined(MIN)
#define MIN( a, b ) ((a>b)?b:a)
#define MAX( a, b ) ((a>b)?a:b)
#endif
#endif
