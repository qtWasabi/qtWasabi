// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once

//
// Win32Shim.h — Win32 typedefs for non-Windows platforms.
//
// Wasabi's API surface uses HWND/HDC/HBITMAP/RECT/POINT throughout
// because it grew up on Win32 in 2002.  When we build it on Linux or
// macOS we still need those types declared so the headers compile.
// We use **opaque-pointer typedefs** for HWND etc. (rather than
// HWND→QWidget*) so internal Wasabi code never accidentally tries to
// memcpy a HWND or pass it through the Win32 API.
//
// The actual mapping HWND → QWidget* (and HDC → QPainter*) lives in
// QtWindowAdapter / QtCanvasAdapter.  Internal Wasabi code receives
// opaque handles; only the adapter knows how to resolve them.
//
// We deliberately keep these typedefs free of Q_OBJECT so the
// handles stay plain opaque cookies.
//

#include <cstdint>

#if defined(_WIN32) || defined(_WIN64)

// Real Windows: include the actual Win32 headers and keep going.
#  include <windows.h>

#else  // Linux, macOS, Asahi, BSD, ...

// Opaque pointer typedefs.  Wasabi treats these as cookies; only
// the Qt adapter knows how to dereference.

struct qtWasabiOpaque_HWND;
struct qtWasabiOpaque_HDC;
struct qtWasabiOpaque_HBITMAP;
struct qtWasabiOpaque_HFONT;
struct qtWasabiOpaque_HPEN;
struct qtWasabiOpaque_HBRUSH;
struct qtWasabiOpaque_HRGN;
struct qtWasabiOpaque_HMENU;
struct qtWasabiOpaque_HMODULE;
struct qtWasabiOpaque_HINSTANCE;
struct qtWasabiOpaque_HCURSOR;
struct qtWasabiOpaque_HICON;

using HWND      = qtWasabiOpaque_HWND*;
using HDC       = qtWasabiOpaque_HDC*;
using HBITMAP   = qtWasabiOpaque_HBITMAP*;
using HFONT     = qtWasabiOpaque_HFONT*;
using HPEN      = qtWasabiOpaque_HPEN*;
using HBRUSH    = qtWasabiOpaque_HBRUSH*;
using HRGN      = qtWasabiOpaque_HRGN*;
using HMENU     = qtWasabiOpaque_HMENU*;
using HMODULE   = qtWasabiOpaque_HMODULE*;
using HINSTANCE = qtWasabiOpaque_HINSTANCE*;
using HCURSOR   = qtWasabiOpaque_HCURSOR*;
using HICON     = qtWasabiOpaque_HICON*;

using BOOL      = int;
using BYTE      = unsigned char;
using WORD      = unsigned short;
using DWORD     = uint32_t;
using LONG      = int32_t;
using ULONG     = uint32_t;
using LPARAM    = intptr_t;
using WPARAM    = uintptr_t;
using LRESULT   = intptr_t;
using UINT      = unsigned int;
using LPVOID    = void*;
using LPCVOID   = const void*;
using HANDLE    = void*;

struct RECT  { LONG left, top, right, bottom; };
struct POINT { LONG x, y; };
struct SIZE  { LONG cx, cy; };

using COLORREF = uint32_t;          // Win32: 0x00BBGGRR

#  ifndef RGB
#    define RGB(r, g, b) (COLORREF)(((BYTE)(r)) | ((BYTE)(g) << 8) | ((BYTE)(b) << 16))
#  endif

#  define GetRValue(c) (BYTE)((c)        & 0xFF)
#  define GetGValue(c) (BYTE)(((c) >>  8) & 0xFF)
#  define GetBValue(c) (BYTE)(((c) >> 16) & 0xFF)

#  ifndef TRUE
#    define TRUE  1
#    define FALSE 0
#  endif

#  ifndef NULL
#    define NULL nullptr
#  endif

#endif  // !_WIN32
