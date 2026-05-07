// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once

//
// wasabi-port-shim.h — force-included into every BFC translation
// unit via -include.  Provides the typedefs/macros that BFC's
// transitively-included std_file.h, std_keyboard.h, std_wnd.h etc.
// reference but the opensourced source never defined for non-Win32/macOS.
//
// These are declarations only.  The Maki VM and script registry —
// the only parts of Wasabi we actually compile — never CALL the file
// or keyboard APIs, so the linker doesn't need an implementation.
// What it does need is for the headers to compile so wasabi_std.h's
// big umbrella include doesn't error out.
//
// Routing the actual file/keyboard/window operations to Qt
// (QFile, QKeyEvent, QtWindowAdapter, QLibrary) happens at the
// WasabiQT layer, above BFC.
//

#if !defined(_WIN32) && !defined(__APPLE__)

// ── replicant/foundation/types.h Linux dispatch ──────────────────
//
// The opensourced replicant/foundation/types.h has Linux + x86_64 only.
// We're targeting aarch64 too (Apple Silicon, Asahi).  Forcing
// __x86_64 makes the dispatch select linux-amd64/types.h whose
// contents (uint32_t / uint64_t / wchar_t) are arch-portable.

#  if defined(__linux__) && !defined(__x86_64) && !defined(__x86_64__)
#    define __x86_64
#    define WASABIQT_FORCED_X86_64_FOR_TYPES_DISPATCH
#  endif

// ── Skip BFC's port-me'd file/keyboard headers ───────────────────
//
// We pre-set their include guards so the bodies are skipped, then
// supply just the typedefs/macros that transitively-including code
// references.  The Maki VM and script registry never CALL these
// functions, so missing implementations never reach the linker.
//
// Wasabi's own std_file.h opening line (verified):
//     #ifndef _STD_FILE_H
//     #define _STD_FILE_H
//     ...   <_WIN32-only impl, #error port me elsewhere>
// Setting _STD_FILE_H here makes the rest of the file evaporate.

#  define _STD_FILE_H 1
#  define NULLSOFT_WASABI_STD_KEYBOARD_H 1

// std_file.h would have provided these (Win32 path):
#  include <stddef.h>
typedef void          *OSFILETYPE;      // opaque
typedef const wchar_t *OSFNCSTR;
#  define WF_READONLY_BINARY  L"rb"
#  define WF_WRITE_TEXT       L"wt"
#  define WF_WRITE_BINARY     L"wb"
#  define WF_APPEND           L"a"
#  define WF_APPEND_RW        L"a+"
#  define OPEN_FAILED         ((OSFILETYPE)0)

// std_keyboard.h would have provided these:
typedef int  OSKEYTYPE;

// ── GUID equality conflict workaround ────────────────────────────
//
// Two upstream files both declare operator== / operator!= for GUID:
//   replicant/foundation/linux-amd64/types.h → returns bool
//   replicant/foundation/guid.h              → returns int (Win32-style)
//
// guid.h's guard reads `!defined(GUID_EQUALS_DEFINED) || !defined(_SYS_GUID_OPERATOR_EQ_)`,
// so it re-declares unless BOTH are pre-set.  We pre-set _SYS_GUID_OPERATOR_EQ_
// here so the bool-returning declarations from types.h win exclusively.
#  define _SYS_GUID_OPERATOR_EQ_ 1

// nsguid.cpp uses these symbols which Wasabi's Win32 build picks up
// from <objbase.h> / <wchar.h>.  Provide them inline.
#  include <stdarg.h>
#  include <string.h>          // memcmp — needed by guid.h's operator<
#  include <memory.h>

// GUID_NULL: an all-zero GUID.  Win32 normally exports it from
// uuid.lib; on Linux we just inline it as a brace-init list.  This
// is a macro so it doesn't need GUID to be defined at this point —
// the dispatched types.h header (linux-amd64/types.h) defines GUID
// later in the include chain, by which time the macro expands fine.
#  ifndef GUID_NULL
#    define GUID_NULL { 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0 } }
#  endif

// WCSNPRINTF: Wasabi's Win32 build maps this to _snwprintf.  POSIX has
// swprintf with the same signature except it always null-terminates,
// which matches Wasabi's actual usage.
#  ifndef WCSNPRINTF
#    define WCSNPRINTF(buf, count, ...) ::swprintf((buf), (count), __VA_ARGS__)
#  endif

// DebugStringW: BFC freelist uses it for end-of-life leak warnings.
// On platforms with no Win32 OutputDebugString, route to stderr.
// Pull <wchar.h> here for fputws (rather than relying on the locale
// block below) so this stays self-contained if the order changes.
#  include <stdio.h>
#  include <wchar.h>
inline void DebugStringW(const wchar_t *s) {
    if (s) ::fputws(s, stderr);
}

// ── Skip precomp + the windowing-related headers we don't use ────
//
// precomp_wasabi_bfc.h pulls wasabi_std_wnd.h which uses HDC and
// is heavily entangled with the X11/Win32 GDI canvas — irrelevant
// for the Maki VM compile.  Set its guard so the umbrella include
// becomes a no-op; supply the actually-needed headers (memblock,
// ptrlist, stack, tlist, std_string, wasabi_std) directly from the
// .cpp files that need them.

#  define NULLSOFT_BFC_PRECOMP_H 1
#  define _STD_WIN_H 1                      // wasabi_std_wnd.h guard

// ── Pre-empt upstream guard symbols so the include-stubs/ versions
//    win even when vcpu.h does `#include "script.h"` (quote-include
//    that resolves locally to the upstream file).
#  define __SCRIPT_H 1                       // api/script/script.h
#  define _SCRIPTOBJI_H 1                    // api/script/scriptobji.h
#  define _SCRIPT_H 1                        // api/script/scriptmgr.h's guard

// ── wasabicfg.h override ─────────────────────────────────────────
//
// Wasabi's wasabicfg.h enables WASABI_COMPILE_WND which then makes
// bfc/platform/linux.h pull X11/Xpm + GTK headers we don't have or
// want.  Pre-set the include guard with a stripped feature set: keep
// SCRIPT (we're vendoring vcpu.cpp/scriptmgr.cpp) and CONFIG, drop
// the windowing stack — our Qt widget layer takes that over.
#  define NULLSOFT_WASABICFG_H 1
#  define WASABI_COMPILE_SCRIPT
#  define WASABI_COMPILE_CONFIG
#  define WASABINOMAINAPI

// ── min/max macro pollution from bfc/platform/linux.h ───────────
// linux.h:91-92 unconditionally `#define min(a,b)` / `#define max(a,b)`,
// which collides with std::min / std::max as soon as <algorithm> or
// <ranges> appears.  Wasabi's own headers don't actually use the
// macros — pre-emptively reserve the identifiers so linux.h's
// definitions become no-ops.
#  define WASABI_NO_MINMAX_MACROS 1
// linux.h does not honour any guard like that, so fall back to
// undef'ing after the fact.  Done in any TU that includes <algorithm>;
// the opensourced vcpu.cpp doesn't, but consumers of the shim might.

// ── locale + wide-char ───────────────────────────────────────────
// wasabi_std.h uses Win32's _locale_t and locale-aware wide-char
// converters without including the right POSIX headers.  Wasabi
// only ever uses the "C" locale for these, so we route through
// the standard non-locale variants.

#  include <wctype.h>
#  include <wchar.h>
#  include <stdlib.h>
#  include <locale.h>
typedef locale_t _locale_t;

static inline _locale_t _create_locale(int /*category*/, const char * /*name*/) {
    return (_locale_t)1;             // Wasabi just checks for non-null
}

static inline int    _wtoi_l(const wchar_t *s, _locale_t)         { return (int)wcstol(s, nullptr, 10); }
static inline double _wtof_l(const wchar_t *s, _locale_t)         { return wcstod(s, nullptr); }
static inline long   _strtol_l(const char *s, char **e, int b, _locale_t) { return strtol(s, e, b); }
static inline double _strtod_l(const char *s, char **e, _locale_t)        { return strtod(s, e); }
static inline long long _strtoi64_l(const char *s, char **e, int b, _locale_t) { return strtoll(s, e, b); }

// Win32 has _wcsdup / _strdup / _wsetlocale / vsprintf_s.  POSIX only
// has the unprefixed forms — alias.
#  define _wcsdup     wcsdup
#  define _strdup     strdup
#  define _wsetlocale wsetlocale_stub
static inline wchar_t *wsetlocale_stub(int category, const wchar_t *) {
    setlocale(category, "C");
    return (wchar_t *)L"C";
}
#  include <stdarg.h>
#  include <stdio.h>
static inline int vsprintf_s(char *buf, size_t cap, const char *fmt, va_list ap) {
    return vsnprintf(buf, cap, fmt, ap);
}

// ── OS handles ───────────────────────────────────────────────────
//
// Wasabi uses HWND/HMODULE/HFONT/HRGN/HCURSOR/HICON across its
// API.  Real X11 platform layer (bfc/platform/linux.h) maps these
// to X11 Window etc. — but we're routing windowing through Qt, so
// we just declare them as opaque pointers and let the script-bridge
// layer translate.

struct WasabiQtOpaque_HMODULE; using HMODULE_QT = WasabiQtOpaque_HMODULE*;
struct WasabiQtOpaque_HINSTANCE; using HINSTANCE_QT = WasabiQtOpaque_HINSTANCE*;
struct WasabiQtOpaque_HWND;     using HWND_QT     = WasabiQtOpaque_HWND*;
struct WasabiQtOpaque_HMENU;    using HMENU_QT    = WasabiQtOpaque_HMENU*;
struct WasabiQtOpaque_HFONT;    using HFONT_QT    = WasabiQtOpaque_HFONT*;
struct WasabiQtOpaque_HRGN;     using HRGN_QT     = WasabiQtOpaque_HRGN*;
struct WasabiQtOpaque_HCURSOR;  using HCURSOR_QT  = WasabiQtOpaque_HCURSOR*;
struct WasabiQtOpaque_HICON;    using HICON_QT    = WasabiQtOpaque_HICON*;

#define OSMODULEHANDLE          HMODULE_QT
#define INVALIDOSMODULEHANDLE   ((OSMODULEHANDLE)0)
#define OSWINDOWHANDLE          HWND_QT
#define INVALIDOSWINDOWHANDLE   ((OSWINDOWHANDLE)0)
#define OSICONHANDLE            HICON_QT
#define INVALIDOSICONHANDLE     ((OSICONHANDLE)0)
#define OSCURSORHANDLE          HCURSOR_QT
#define INVALIDOSCURSORHANDLE   ((OSCURSORHANDLE)0)
#define OSREGIONHANDLE          HRGN_QT
#define INVALIDOSREGIONHANDLE   ((OSREGIONHANDLE)0)
#define OSTHREADHANDLE          void*
#define INVALIDOSTHREADHANDLE   ((OSTHREADHANDLE)0)
typedef HMENU_QT  OSMENUHANDLE;
typedef HFONT_QT  OSFONTHANDLE;

// RECT / POINT / SIZE — bfc/platform/linux.h already typedef's
// these (with `int` members) when LINUX is defined.  Don't
// redeclare here.

// ── min/max macro cleanup ────────────────────────────────────────
// bfc/platform/linux.h:91-92 unconditionally:
//     #define min(a,b) (((a)<(b))?(a):(b))
//     #define max(a,b) (((a)>(b))?(a):(b))
// which collides with std::min / std::max as soon as <algorithm> /
// <format> appears.  We undefine them after-the-fact via this small
// header that consumers include AFTER any Wasabi header.  Most TUs
// don't need this, but the public Maki test does.

#endif  // !_WIN32 && !__APPLE__

// Defined regardless of platform — convenience macro for downstream
// to include after a Wasabi header to clean up its macro pollution.
// Usage:  #include <wasabi-port-cleanmacros.h>  // see header.
