// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once
//
// shellapi.h — Win32 Shell32 API surface used by ml_playlists for
// ShellExecute and DragAcceptFiles.  Linux/macOS have no equivalent
// — return-failure stubs.
//

#include "basetsd.h"
#include "windef.h"
#include "winuser.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef HANDLE HDROP;

HINSTANCE WINAPI ShellExecuteW(HWND, LPCWSTR verb, LPCWSTR file,
                                 LPCWSTR params, LPCWSTR dir, INT show);
HINSTANCE WINAPI ShellExecuteA(HWND, LPCSTR  verb, LPCSTR  file,
                                 LPCSTR  params, LPCSTR  dir, INT show);
BOOL      WINAPI DragAcceptFiles(HWND, BOOL);
void      WINAPI DragFinish     (HDROP);
UINT      WINAPI DragQueryFileW (HDROP, UINT, LPWSTR, UINT);
UINT      WINAPI DragQueryFileA (HDROP, UINT, LPSTR,  UINT);

#if defined(UNICODE) || defined(_UNICODE)
#  define ShellExecute  ShellExecuteW
#  define DragQueryFile DragQueryFileW
#else
#  define ShellExecute  ShellExecuteA
#  define DragQueryFile DragQueryFileA
#endif

#ifdef __cplusplus
}  // extern "C"
#endif
