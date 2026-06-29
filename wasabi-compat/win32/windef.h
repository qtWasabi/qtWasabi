// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#ifndef _WINDEF_H_
#define _WINDEF_H_
//
// windef.h — Win32's "core types and macros" header.  Pulled in by
// every other Win32 header.  Defines BOOL/BYTE/DWORD/RECT/POINT
// type family, calling convention macros (WINAPI, CALLBACK), and
// the HANDLE-family opaque types.
//
// Our opaque handles are pointer-sized typedefs; the actual backing
// objects live in the wasabi-compat handle registry.
//

#include "basetsd.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Calling conventions ──────────────────────────────────────────
// Windows uses these as `__stdcall`/`__cdecl`-style annotations.
// On Linux/macOS the System V ABI is the only calling convention,
// so the macros expand to empty.
#define WINAPI
#define CALLBACK
#define APIENTRY
#define WINAPIV
#define APIPRIVATE
#define PASCAL
#define NEAR
#define FAR
#define CONST                  const
#define VOID                   void
#define IN
#define OUT
#define OPTIONAL
#define UNALIGNED

// ── Fundamental scalar types ────────────────────────────────────
//
// Win32's LONG/ULONG are SPECIFICALLY 32-bit even on 64-bit
// targets (Microsoft kept `long` 32-bit on Windows x64).  We must
// hold the same invariant for RECT/POINT/BITMAPINFOHEADER and any
// struct gen_ml passes across the message boundary to stay wire-
// compatible.  On Linux/macOS native `long` is 64-bit, so we alias
// LONG to int32_t directly.
typedef int                    BOOL,    *PBOOL,   *LPBOOL;
typedef unsigned char          BYTE,    *PBYTE,   *LPBYTE;
typedef unsigned short         WORD,    *PWORD,   *LPWORD;
typedef uint32_t               DWORD,   *PDWORD,  *LPDWORD;
typedef uint64_t               QWORD,   *PQWORD;
typedef unsigned int           UINT,    *PUINT;
typedef int                    INT,     *PINT,    *LPINT;
typedef int32_t                LONG,    *PLONG,   *LPLONG;
typedef uint32_t               ULONG,   *PULONG;
typedef short                  SHORT,   *PSHORT;
typedef unsigned short         USHORT,  *PUSHORT;
typedef char                   CHAR,    *PCHAR;
typedef wchar_t                WCHAR,   *PWCHAR;
typedef float                  FLOAT,   *PFLOAT;
typedef double                 DOUBLE,  *PDOUBLE;
typedef uint8_t                UCHAR,   *PUCHAR;

// String pointer family.
typedef const char            *LPCSTR,  *PCSTR;
typedef char                  *LPSTR,   *PSTR;
typedef const wchar_t         *LPCWSTR, *PCWSTR;
typedef wchar_t               *LPWSTR,  *PWSTR;
// TCHAR maps to char OR wchar_t depending on UNICODE define.
// Match Win32 SDK behaviour so tchar.h's macros + windef.h
// declarations agree.
#if defined(UNICODE) || defined(_UNICODE)
typedef WCHAR                   TCHAR,   *PTCHAR;
typedef LPWSTR                 LPTSTR;
typedef LPCWSTR                LPCTSTR;   // must track WCHAR under UNICODE so
                                          // MAKEINTRESOURCE(=W)→LPCTSTR agrees
#else
typedef CHAR                    TCHAR,   *PTCHAR;
typedef LPSTR                  LPTSTR;
typedef LPCSTR                 LPCTSTR;
#endif
typedef void                  *PVOID,   *LPVOID;
typedef const void            *PCVOID,  *LPCVOID;

// ── Boolean constants ───────────────────────────────────────────
#ifndef TRUE
#define TRUE                   1
#endif
#ifndef FALSE
#define FALSE                  0
#endif
#ifndef NULL
#ifdef __cplusplus
#define NULL                   nullptr
#else
#define NULL                   ((void *)0)
#endif
#endif

// ── Opaque handle macro family ──────────────────────────────────
// Win32 uses DECLARE_HANDLE for distinct opaque-pointer types so
// the compiler catches mixing e.g. HWND with HMENU.  We honour that
// — the actual instances are managed via the wasabi-compat handle
// registry, not by the kernel.
#define DECLARE_HANDLE(name)   struct name##__ { int unused; }; \
                               typedef struct name##__ *name

// HANDLE is the Win32 generic "any opaque pointer" type — used
// by SetProp/GetProp to store arbitrary plugin pointers,
// CreateFile for kernel handles, etc.  Win32 SDK defines it as
// `typedef PVOID HANDLE` (i.e. void*) precisely so callers can
// pass any pointer through it.  Keep DECLARE_HANDLE for the
// type-distinct handles below (HWND, HMENU, etc.), but HANDLE
// itself stays loose.
typedef void *HANDLE;
typedef HANDLE *PHANDLE;
typedef HANDLE *LPHANDLE;

DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HICON);
DECLARE_HANDLE(HCURSOR);
DECLARE_HANDLE(HINSTANCE);

// GDI object handles are void* — real Win32 non-strict semantics.
// Winamp's wa_dlg / gen_ml pass HBITMAP/HPEN/HBRUSH/HFONT/HRGN straight
// into SelectObject(HDC, HGDIOBJ) and DeleteObject(HGDIOBJ), relying on
// the implicit void* conversion every GDI handle has.  Strict
// DECLARE_HANDLE distinct types broke that ("cannot convert HPEN to
// HGDIOBJ").  We keep the type-distinct handles above (HWND/HMENU/HDC/
// HICON/…) where the safety matters and nothing needs to interconvert;
// the GDI object family below is deliberately loose, exactly as the SDK
// is without STRICT.  The wasabi-compat handle registry disambiguates by
// the C++ backing TYPE (HandleTraits<BitmapObject>), not the handle type,
// so losing the compile-time distinction here is safe.
typedef void *HGDIOBJ;
typedef void *HBITMAP;
typedef void *HBRUSH;
typedef void *HPEN;
typedef void *HFONT;
typedef void *HRGN;
typedef void *HPALETTE;
// HMODULE — Win32 SDK aliases this to HINSTANCE so the two are
// interchangeable.  Plugin code casts back and forth.
typedef HINSTANCE HMODULE;
// HGLOBAL / HLOCAL — Win32 SDK uses plain HANDLE (void*) for these.
// Allows code like `wchar_t *p = GlobalAlloc(...); GlobalFree(p);`
// which legacy ml_* TUs do.
typedef HANDLE HGLOBAL;
typedef HANDLE HLOCAL;
DECLARE_HANDLE(HKEY);
DECLARE_HANDLE(HACCEL);
DECLARE_HANDLE(HIMAGELIST);
DECLARE_HANDLE(HMETAFILE);
DECLARE_HANDLE(HRSRC);
DECLARE_HANDLE(HKL);
DECLARE_HANDLE(HDWP);
DECLARE_HANDLE(HMONITOR);
DECLARE_HANDLE(HCOLORSPACE);
DECLARE_HANDLE(HDESK);
DECLARE_HANDLE(HWINSTA);
DECLARE_HANDLE(HHOOK);

typedef HICON                  HICON_PTR;
typedef HINSTANCE              HMODULE_ALIAS;  // sometimes aliased

// ── Message-loop scalar types ──────────────────────────────────
typedef UINT_PTR               WPARAM;
typedef LONG_PTR               LPARAM;
typedef LONG_PTR               LRESULT;
typedef int32_t                HRESULT;
typedef DWORD                  COLORREF;
typedef DWORD                 *LPCOLORREF;

// SAL source-annotation macros — MSVC parameter annotations that appear
// in Winamp headers (e.g. dpi.h's `ScaleRect(__inout RECT*)`).  No-ops.
// NOTE: deliberately do NOT define bare __in / __out — libstdc++ uses
// those as internal variable names (stl_algobase/stl_pair), so blanking
// them breaks the STL.  Only the annotations Winamp headers actually use.
// Only __inout (used by dpi.h).  __reserved/__notnull collide with
// linux/stat.h struct members; __in/__out collide with libstdc++ — so
// none of those are defined here.
#ifndef _SAL_NOOPS_DEFINED
#define _SAL_NOOPS_DEFINED 1
#define __inout
#define __inout_opt
#endif

// HWND messageless versions (some Wasabi code uses these as
// distinct types — alias to the real handle).
typedef HWND                   HWNDPARENT;

// ── Geometry primitives ────────────────────────────────────────
typedef struct tagPOINT {
    LONG x;
    LONG y;
} POINT, *PPOINT, *LPPOINT;

typedef struct tagPOINTS {
    SHORT x;
    SHORT y;
} POINTS, *PPOINTS, *LPPOINTS;

typedef struct tagSIZE {
    LONG cx;
    LONG cy;
} SIZE, *PSIZE, *LPSIZE;

typedef struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT, *PRECT, *LPRECT;

typedef const RECT             *LPCRECT;

typedef struct tagRECTL {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECTL, *PRECTL, *LPRECTL;

// ── Constants ──────────────────────────────────────────────────
#define MAX_PATH               260
#define INVALID_HANDLE_VALUE   ((HANDLE)(LONG_PTR)-1)

// LOWORD/HIWORD/MAKELONG/MAKEWORD — used pervasively in Win32 IPC.
#define LOWORD(l)              ((WORD)(((DWORD_PTR)(l)) & 0xffff))
#define HIWORD(l)              ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))
#define LOBYTE(w)              ((BYTE)(((DWORD_PTR)(w)) & 0xff))
#define HIBYTE(w)              ((BYTE)((((DWORD_PTR)(w)) >> 8) & 0xff))
#define MAKEWORD(a, b)         ((WORD)(((BYTE)((DWORD_PTR)(a) & 0xff)) | \
                                       ((WORD)((BYTE)((DWORD_PTR)(b) & 0xff))) << 8))
#define MAKELONG(a, b)         ((LONG)(((WORD)((DWORD_PTR)(a) & 0xffff)) | \
                                       ((DWORD)((WORD)((DWORD_PTR)(b) & 0xffff))) << 16))
#define MAKEWPARAM(l, h)       ((WPARAM)(DWORD)MAKELONG(l, h))
#define MAKELPARAM(l, h)       ((LPARAM)(DWORD)MAKELONG(l, h))
#define MAKELRESULT(l, h)      ((LRESULT)(DWORD)MAKELONG(l, h))

// RGB triplet packer.
#define RGB(r, g, b)           ((COLORREF)(((BYTE)(r)) | \
                                           (((WORD)((BYTE)(g))) << 8) | \
                                           (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(rgb)         (LOBYTE(rgb))
#define GetGValue(rgb)         (LOBYTE(((WORD)(rgb)) >> 8))
#define GetBValue(rgb)         (LOBYTE((rgb) >> 16))

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // _WINDEF_H_
