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
// Reference (not source): /Src/Wasabi/qt6/win32_types.h.  That file
// targets a 2015-era Qt and pulls Q_OBJECT into the typedefs; we
// avoid both.
//

#include <cstdint>

#if defined(_WIN32) || defined(_WIN64)

// Real Windows: include the actual Win32 headers and keep going.
#  include <windows.h>

#else  // Linux, macOS, Asahi, BSD, ...

// Opaque pointer typedefs.  Wasabi treats these as cookies; only
// the Qt adapter knows how to dereference.

struct WasabiQtOpaque_HWND;
struct WasabiQtOpaque_HDC;
struct WasabiQtOpaque_HBITMAP;
struct WasabiQtOpaque_HFONT;
struct WasabiQtOpaque_HPEN;
struct WasabiQtOpaque_HBRUSH;
struct WasabiQtOpaque_HRGN;
struct WasabiQtOpaque_HMENU;
struct WasabiQtOpaque_HMODULE;
struct WasabiQtOpaque_HINSTANCE;
struct WasabiQtOpaque_HCURSOR;
struct WasabiQtOpaque_HICON;

using HWND      = WasabiQtOpaque_HWND*;
using HDC       = WasabiQtOpaque_HDC*;
using HBITMAP   = WasabiQtOpaque_HBITMAP*;
using HFONT     = WasabiQtOpaque_HFONT*;
using HPEN      = WasabiQtOpaque_HPEN*;
using HBRUSH    = WasabiQtOpaque_HBRUSH*;
using HRGN      = WasabiQtOpaque_HRGN*;
using HMENU     = WasabiQtOpaque_HMENU*;
using HMODULE   = WasabiQtOpaque_HMODULE*;
using HINSTANCE = WasabiQtOpaque_HINSTANCE*;
using HCURSOR   = WasabiQtOpaque_HCURSOR*;
using HICON     = WasabiQtOpaque_HICON*;

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

#  ifndef TRUE
#    define TRUE  1
#    define FALSE 0
#  endif

#  ifndef NULL
#    define NULL nullptr
#  endif

#endif  // !_WIN32
