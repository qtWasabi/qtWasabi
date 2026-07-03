// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once
//
// compat-shim.h — force-included into every ported gen_ml / ml_*
// translation unit via the CMake `-include` flag.
//
// Sibling intent to wasabi-port-shim.h but at a different layer:
// wasabi-port papers over BFC's unported Linux/macOS hooks (file,
// keyboard, window).  This one papers over the Win32 + Wasabi-API
// gap a real ml plugin assumes — it pulls in our `win32/` shim
// headers in the right order, applies the same overflow/register
// warning relaxations, and pre-defines guard macros so deeper
// Win32 system headers (`windowsx.h`, `winuser.h`, …) that the
// upstream code accidentally includes resolve to our stubs rather
// than to host system headers.
//
// The Win32 typedefs and API stubs below grow as more of the ml
// plugin surface is exercised.

#if defined(_WIN32) || defined(_WIN64)
// On native Windows the shim is a no-op — the real Win32 headers
// already do everything.  qtWasabi runs there too but uses the
// system <windows.h>.
#  define QTWASABI_COMPAT_NATIVE_WIN32 1
#endif

#if !defined(QTWASABI_COMPAT_NATIVE_WIN32)

// ── Marker ──────────────────────────────────────────────────────
// Translation units can `#ifdef QTWASABI_COMPAT_SHIM_INCLUDED` to
// gate platform branches the upstream Win32 code didn't anticipate.
#  define QTWASABI_COMPAT_SHIM_INCLUDED 1

// Compilation guards for headers we intercept.  Setting their
// include guards here means a `#include <windows.h>` from upstream
// code resolves through our shim (which #define's the real guard
// before declaring its content).  Flipped on per-header as each
// shim is added.

// Force-include the COM subset.  Upstream plugin code mixes
// IDispatch / STDMETHOD / REFIID / DISPID without explicitly
// including <objbase.h>; this keeps them available everywhere.
// Also pulls <cstring> for `memcmp` that BFC's guid.h calls
// without bringing its own include.
// Suppress BFC linux.h's `#define min(a,b)` / `#define max(a,b)`.
#  ifdef min
#    undef min
#  endif
#  ifdef max
#    undef max
#  endif

// ── Architecture force-define + BFC pre-skip guards ────────────
// These MUST come before any STL include — `<cassert>` resolves
// via our include paths to Wasabi's `bfc/assert.h`, which chains
// into `wasabi_std.h` → `platform/types.h` → `replicant/foundation/
// types.h` and would hit `#error port me!` without `__x86_64`
// and pre-set guards.
#  if defined(__linux__) && !defined(__x86_64) && !defined(__x86_64__)
#    define __x86_64
#  endif
// The ml_*/pledit targets fake _MSC_VER so upstream Win32 source
// compiles.  On the macOS SDK, clang's own <stddef.h> then follows
// MSVC's wchar_t codepath and re-typedefs the wchar_t keyword,
// colliding with it ("cannot combine with previous 'int'").  In C++
// wchar_t is native, so set the MSVC "wchar_t is native" marker to
// suppress that typedef.  A no-op on glibc, required on macOS.
#  if defined(_MSC_VER) && !defined(_NATIVE_WCHAR_T_DEFINED)
#    define _NATIVE_WCHAR_T_DEFINED 1
#  endif
// BFC's wasabi_std.h declares a pthread_t member; glibc pulls <pthread.h>
// in transitively, the macOS SDK does not — include it explicitly.
#  include <pthread.h>
#  define _STD_FILE_H 1
#  define NULLSOFT_WASABI_STD_KEYBOARD_H 1
#  define __TIMER_API_H 1
#  define __LINUX_H_WASABI 1
#  ifndef NOMINMAX
#    define NOMINMAX 1
#  endif
#  define _SYS_GUID_OPERATOR_EQ_ 1

// Pre-define Win32 types BFC's `<cassert>` → `wasabi_std_rect.h`
// chain needs (RECT, POINT, SIZE, COLORREF, etc.).  windef.h is
// header-only typedefs — no transitive BFC inclusion.
#  include "win32/windef.h"
#  include "win32/winuser.h"     // dialog/window APIs
#  include "win32/shellapi.h"    // ShellExecuteW (upstream omits its include)

// BFC's wasabi_std.h also references OSWINDOWHANDLE et al at the
// `#include <cassert>` point.  Define them inline now (mirrors
// the block further down for plugin TUs that include compat-shim
// without hitting the BFC chain via <cassert>).
typedef HCURSOR        OSCURSOR;
typedef HWND           OSWINDOWHANDLE;
typedef HMODULE        OSMODULEHANDLE;
typedef HICON          OSICON;
typedef HRGN           OSREGION;
typedef HMENU          OSMENU;
typedef HBITMAP        OSBITMAP;
typedef const wchar_t *OSFNCSTR;
typedef wchar_t       *OSFNSTR;
#  define INVALIDOSCURSORHANDLE       ((OSCURSOR)0)
#  define INVALIDOSWINDOWHANDLE       ((OSWINDOWHANDLE)0)
#  define INVALIDOSMODULEHANDLE       ((OSMODULEHANDLE)0)

// Pre-pull standard C headers BFC's wasabi_std.h uses (toupper,
// tolower, towupper, _locale_t).  These DON'T trigger BFC's
// shadow include (no bfc/wctype.h etc.) — only `<cassert>` does.
#  include <cctype>        // toupper / tolower
#  include <cstdlib>       // strtol / wcstol family
#  include <cstring>
#  include <cwchar>        // wcstol, wcstod
#  include <cwctype>       // std::towlower used by CompareStringW

// objbase.h provides `_locale_t` + the _wtoi/_wtof_l family BFC's
// `wasabi_std.h` references via inline definitions; pull it
// before the BFC-triggering `<cassert>`.
#  include "win32/objbase.h"
#  include "win32/strsafe.h"   // SUCCEEDED/FAILED, StringCch* family

// Bring `toupper`/`tolower`/`towupper`/`towlower` to the global
// scope — `<cctype>` and `<cwctype>` deposit them inside namespace
// `std::` on libstdc++, but BFC's `wasabi_std.h` uses unqualified
// names.
#  ifdef __cplusplus
using std::toupper;
using std::tolower;
using std::towupper;
using std::towlower;
#  endif

// NOW pull `<cassert>` — its `assert.h` resolves to BFC's shadow
// header which chains into the rest of BFC's std surface.  Every
// prerequisite typedef has been placed above.
#  include <cassert>

// BFC's `bfc/assert.h` shadows the system one — defines `ASSERT`
// macro but NOT lowercase `assert()`.  System `<cassert>` would
// normally provide it via libc; provide our own here so plugin
// `assert(expr)` calls compile.  Routes to libc's __assert_fail.
#  ifdef __cplusplus
extern "C" void __assert_fail(const char *, const char *, unsigned int,
                                const char *) noexcept __attribute__((noreturn));
#  endif
#  ifndef assert
#    define assert(expr)                                            \
       ((expr) ? void(0) :                                          \
        __assert_fail(#expr, __FILE__, __LINE__, __func__))
#  endif

// Provide bare `min` / `max` as function templates in the global
// namespace.  Win32 plugin code does `min(a, b)` / `max(a, b)`
// expecting the Win32 SDK's `windef.h` macros.  We can't enable
// those macros (collide with `std::min` / `std::max` in
// `<algorithm>`), but a function template at global scope satisfies
// the same call sites without breaking the STL — `std::min` calls
// remain qualified.
#  ifdef __cplusplus
template<typename T> inline T min(T a, T b) { return a < b ? a : b; }
template<typename T> inline T max(T a, T b) { return a > b ? a : b; }
#  endif

// ── Replicant GUID operator== guard ─────────────────────────────
// Both replicant/foundation/linux-amd64/types.h and replicant/
// foundation/guid.h declare GUID operator== / operator!= — the
// former returns bool, the latter int.  Pre-set the guid.h guard
// so the bool-returning types.h declarations win exclusively.
// Mirrors wasabi-port-shim.h's identical fix.
#  define _SYS_GUID_OPERATOR_EQ_ 1

// Win32 MSVC-isms used by upstream code.
#  ifndef __int64
typedef long long __int64;
#  endif
#  ifndef _LARGE_INTEGER_DEFINED
typedef long long __time64_t;
#  endif

// ── replicant/foundation/types.h dispatch ───────────────────────
// Mirrors wasabi-port-shim's `__x86_64` force-define so the
// types.h Linux dispatch picks the linux-amd64 branch.  Without
// this, types.h hits its `#error port me!`.  The forced define
// is architecture-agnostic for our purposes — types.h only uses
// it to choose a header; we're aarch64-portable downstream.
#  if defined(__linux__) && !defined(__x86_64) && !defined(__x86_64__)
#    define __x86_64
#  endif

// ── BFC port-me files pre-skip ──────────────────────────────────
// These upstream files have `#error port me!` blocks Wasabi
// never finished porting.  We don't compile their bodies; the
// shims that follow declare the typedefs they would have brought.
// Mirrors wasabi-port-shim's identical trick.
#  define _STD_FILE_H 1
#  define NULLSOFT_WASABI_STD_KEYBOARD_H 1
#  define __TIMER_API_H 1

// ── BFC platform/linux.h pre-skip ───────────────────────────────
// Upstream BFC ships a hand-rolled `platform/linux.h` from 2003
// that re-defines HWND / RECT / POINT / COLORREF / TCHAR / DWORD /
// WORD / SIZE / LONG / LRESULT / LPARAM / WPARAM / RGB / LOWORD /
// HIWORD / MAX_PATH / __declspec / __cdecl / EXTC / min / max
// directly — and conflicts with the OS-aligned shapes our
// `windef.h` provides.  Pre-set its include guard so the body is
// skipped; every type it would have declared comes from our
// own win32/ shim instead.
//
// (wasabi-port already uses the same trick for `std_file.h` etc.
// via `_STD_FILE_H 1`.)
#  define __LINUX_H_WASABI 1

// Suppress min/max macros — they collide with `<algorithm>`.  Any
// upstream code that wants them can use `std::min` / `std::max`
// which our windef.h's friends already enable through algorithm.
#  ifndef NOMINMAX
#    define NOMINMAX 1
#  endif

// ── Misc Win32 type shims ────────────────────────────────────────
// `_locale_t` is MSVC's locale handle.  We don't ship locale-aware
// translation; alias to int so vtables that name it still compile.
#ifndef _LOCALE_T_DEFINED
#define _LOCALE_T_DEFINED
typedef int _locale_t;
#endif

// InterlockedIncrement / Decrement — Win32 atomic intrinsics
// upstream code uses for refcounting.  Back with GCC's
// `__atomic_*` builtins (sequentially-consistent ordering, same
// guarantees as MSVC).
static inline LONG InterlockedIncrement(LONG volatile *target) {
    return __atomic_add_fetch(target, 1, __ATOMIC_SEQ_CST);
}
static inline LONG InterlockedDecrement(LONG volatile *target) {
    return __atomic_sub_fetch(target, 1, __ATOMIC_SEQ_CST);
}
static inline LONG InterlockedExchange(LONG volatile *target, LONG value) {
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}
static inline LONG InterlockedCompareExchange(LONG volatile *dst,
                                                 LONG exchange,
                                                 LONG comparand) {
    __atomic_compare_exchange_n(dst, &comparand, exchange, false,
                                  __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return comparand;
}

// BFC platform-abstraction typedefs.  BFC's platform/linux.h would
// have provided these as native-handle aliases, but we pre-skip that
// header (see above) so define them here instead.  Plugin code uses
// OSCURSOR etc. as the cross-platform name for what's HCURSOR on
// Win32.
typedef HCURSOR        OSCURSOR;
typedef HWND           OSWINDOWHANDLE;
typedef HMODULE        OSMODULEHANDLE;
typedef HICON          OSICON;
typedef HRGN           OSREGION;
typedef HMENU          OSMENU;
typedef HBITMAP        OSBITMAP;
typedef const wchar_t *OSFNCSTR;   // file-name C-string
typedef wchar_t       *OSFNSTR;
#define INVALIDOSCURSORHANDLE       ((OSCURSOR)0)
#define INVALIDOSWINDOWHANDLE       ((OSWINDOWHANDLE)0)
#define INVALIDOSMODULEHANDLE       ((OSMODULEHANDLE)0)

// `FAR` / `NEAR` — Win16 leftover macros.  Modern Win32 leaves
// them defined as empty.  Upstream Wasabi code still uses them in
// IDispatch vtable declarations.
#  ifndef FAR
#    define FAR
#  endif
#  ifndef NEAR
#    define NEAR
#  endif
#  ifndef PASCAL
#    define PASCAL
#  endif

// `interface` — MSVC keyword for COM interfaces.  Standard C++
// uses `struct` for the same role.
#  ifndef interface
#    define interface struct
#  endif

// `IS_INTRESOURCE` — Win32 helper that checks whether a resource
// pointer is actually an integer ID (high bits all zero).
#  ifndef IS_INTRESOURCE
#    define IS_INTRESOURCE(_r) ((((ULONG_PTR)(_r)) >> 16) == 0)
#  endif

// CopyMemory / FillMemory / ZeroMemory / MoveMemory — Win32
// aliases for the cstring functions.
#  ifndef CopyMemory
#    define CopyMemory(dst, src, n)   memcpy((dst), (src), (n))
#  endif
#  ifndef FillMemory
#    define FillMemory(dst, n, fill)  memset((dst), (fill), (n))
#  endif
#  ifndef ZeroMemory
#    define ZeroMemory(dst, n)        memset((dst), 0, (n))
#  endif
#  ifndef MoveMemory
#    define MoveMemory(dst, src, n)   memmove((dst), (src), (n))
#  endif

// lstrlenW / lstrlenA — Win32 length helpers.  Same semantics as
// wcslen / strlen with no-null safety.
static inline int lstrlenW(const wchar_t *s) {
    if (!s) return 0;
    int n = 0; while (s[n]) ++n; return n;
}
static inline int lstrlenA(const char *s) {
    if (!s) return 0;
    int n = 0; while (s[n]) ++n; return n;
}
#  if defined(UNICODE) || defined(_UNICODE)
#    define lstrlen lstrlenW
#  else
#    define lstrlen lstrlenA
#  endif

// WideCharToMultiByte / MultiByteToWideChar — Win32 string
// transcoders.  Real Win32 honours code-pages; we just pump through
// the C wcstombs / mbstowcs assuming the host locale is UTF-8.
static inline int WideCharToMultiByte(UINT /*cp*/, DWORD /*flags*/,
                                        const wchar_t *src, int srcLen,
                                        char *dst, int dstLen,
                                        const char * /*defaultChar*/,
                                        BOOL * /*usedDefaultChar*/) {
    if (!src) return 0;
    if (srcLen < 0) srcLen = lstrlenW(src);
    if (!dst || dstLen == 0) {
        // Return required byte count (worst-case: srcLen * 4 for UTF-8).
        return srcLen * 4 + 1;
    }
    int written = 0;
    for (int i = 0; i < srcLen && written + 1 < dstLen; ++i) {
        if (src[i] < 128) {
            dst[written++] = (char)src[i];
        } else {
            // Best-effort ASCII-only fallback.  A real iconv-backed
            // transcode would replace this if a caller needs it.
            dst[written++] = '?';
        }
    }
    if (written < dstLen) dst[written] = 0;
    return written;
}
static inline int MultiByteToWideChar(UINT /*cp*/, DWORD /*flags*/,
                                        const char *src, int srcLen,
                                        wchar_t *dst, int dstLen) {
    if (!src) return 0;
    if (srcLen < 0) srcLen = lstrlenA(src);
    if (!dst || dstLen == 0) return srcLen + 1;
    int written = 0;
    for (int i = 0; i < srcLen && written + 1 < dstLen; ++i) {
        dst[written++] = (wchar_t)(unsigned char)src[i];
    }
    if (written < dstLen) dst[written] = 0;
    return written;
}

// Code-page constants WideCharToMultiByte callers pass.
#  define CP_ACP    0
#  define CP_OEMCP  1
#  define CP_UTF8   65001

// lstrcpynW / lstrcpynA — Win32 length-limited string copy.
// Returns dst.  Semantics: at most n characters including null.
static inline wchar_t *lstrcpynW(wchar_t *dst, const wchar_t *src, int n) {
    if (!dst || n <= 0) return dst;
    if (!src) { dst[0] = 0; return dst; }
    int i = 0;
    for (; i + 1 < n && src[i] != 0; ++i) dst[i] = src[i];
    dst[i] = 0;
    return dst;
}
static inline char *lstrcpynA(char *dst, const char *src, int n) {
    if (!dst || n <= 0) return dst;
    if (!src) { dst[0] = 0; return dst; }
    int i = 0;
    for (; i + 1 < n && src[i] != 0; ++i) dst[i] = src[i];
    dst[i] = 0;
    return dst;
}
#  if defined(UNICODE) || defined(_UNICODE)
#    define lstrcpyn lstrcpynW
#  else
#    define lstrcpyn lstrcpynA
#  endif

// CompareString constants + function.  Real Win32 honours locale
// + flags; our stub does case-insensitive ASCII compare.  A real
// implementation would replace this if a caller needs it.
#  define CSTR_LESS_THAN    1
#  define CSTR_EQUAL        2
#  define CSTR_GREATER_THAN 3
#  define NORM_IGNORECASE   0x00000001
#  define NORM_IGNOREWIDTH  0x00020000
#  define LINGUISTIC_IGNORECASE 0x00000010
#  define LOCALE_INVARIANT  0x007F
static inline int CompareStringW(LCID /*locale*/, DWORD flags,
                                   const wchar_t *s1, int n1,
                                   const wchar_t *s2, int n2) {
    if (!s1 || !s2) return 0;
    if (n1 < 0) n1 = lstrlenW(s1);
    if (n2 < 0) n2 = lstrlenW(s2);
    const bool ci = (flags & (NORM_IGNORECASE | LINGUISTIC_IGNORECASE)) != 0;
    int i = 0;
    while (i < n1 && i < n2) {
        wchar_t a = ci ? std::towlower(s1[i]) : s1[i];
        wchar_t b = ci ? std::towlower(s2[i]) : s2[i];
        if (a != b) return a < b ? CSTR_LESS_THAN : CSTR_GREATER_THAN;
        ++i;
    }
    if (i < n1) return CSTR_GREATER_THAN;
    if (i < n2) return CSTR_LESS_THAN;
    return CSTR_EQUAL;
}
#  if defined(UNICODE) || defined(_UNICODE)
#    define CompareString CompareStringW
#  else
#    define CompareString CompareStringW
#  endif

// CreateDirectory — Win32 file API.  Stub to no-op success.
#  ifdef __cplusplus
extern "C" {
#  endif
#ifndef _SECURITY_ATTRIBUTES_DEFINED
#define _SECURITY_ATTRIBUTES_DEFINED 1
typedef struct _SECURITY_ATTRIBUTES {
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;
#endif

static inline BOOL CreateDirectoryW(LPCWSTR /*path*/,
                                      LPSECURITY_ATTRIBUTES /*sa*/) {
    return TRUE;
}
static inline BOOL CreateDirectoryA(LPCSTR /*path*/,
                                      LPSECURITY_ATTRIBUTES /*sa*/) {
    return TRUE;
}
#  if defined(UNICODE) || defined(_UNICODE)
#    define CreateDirectory CreateDirectoryW
#  else
#    define CreateDirectory CreateDirectoryA
#  endif
#  ifdef __cplusplus
}  // extern "C"
#  endif

// ── Win32 keyword / linkage shims ───────────────────────────────
// Upstream plugin code uses Windows-only linkage / calling-
// convention keywords.  On non-Windows they have no equivalent;
// expand to nothing so the source compiles.
#  ifndef __declspec
#    define __declspec(x)
#  endif
#  ifndef EXTERN_C
#    ifdef __cplusplus
#      define EXTERN_C extern "C"
#    else
#      define EXTERN_C extern
#    endif
#  endif
#  ifndef __cdecl
#    define __cdecl
#  endif
#  ifndef __stdcall
#    define __stdcall
#  endif
#  ifndef __fastcall
#    define __fastcall
#  endif
#  ifndef __forceinline
#    define __forceinline inline
#  endif

#endif  // !QTWASABI_COMPAT_NATIVE_WIN32
