// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once
//
// windowsx.h — Win32 "extended" macros.  Common ones:
//   GET_X_LPARAM / GET_Y_LPARAM — extract signed POINT coords from
//                                 an LPARAM (used by mouse-event
//                                 WM_MOUSEMOVE / WM_LBUTTONDOWN).
//   GET_WM_COMMAND_ID            — extract control id from WPARAM.
// ml_playlists uses these through `nu/listview.h`.
//

#include "basetsd.h"
#include "windef.h"

#define GET_X_LPARAM(lp)         (static_cast<int>(static_cast<short>(LOWORD(lp))))
#define GET_Y_LPARAM(lp)         (static_cast<int>(static_cast<short>(HIWORD(lp))))

// WM_COMMAND wparam decoding.
#define GET_WM_COMMAND_ID(wp, lp)   LOWORD(wp)
#define GET_WM_COMMAND_HWND(wp, lp) ((HWND)(lp))
#define GET_WM_COMMAND_CMD(wp, lp)  HIWORD(wp)

// Convenience message senders ml_playlists uses.
#define SetWindowFont(hwnd, hfont, redraw) \
    SendMessageW((hwnd), 0x30 /* WM_SETFONT */, (WPARAM)(hfont), \
                  MAKELPARAM(redraw, 0))
#define GetWindowFont(hwnd) \
    ((HFONT)SendMessageW((hwnd), 0x31 /* WM_GETFONT */, 0, 0))

#define EnableWindowEx(hwnd, enable)    EnableWindow((hwnd), (enable))
