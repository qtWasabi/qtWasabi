// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// wa-dlg-impl.cpp — compiles the wa_dlg dialog-skinning code.
//
// `wa_dlg.h` is header-only: defining WA_DLG_IMPLEMENT pulls in the
// bodies for WADlg_init / WADlg_getColor / WADlg_handleDialogMsgs
// (the silver-button 9-slice WM_DRAWITEM draw) / WADlg_DrawChildWindow
// Borders (sunken-border + divider bevels).  gen_ml + the ml_* plugins
// call these via the WADlg_ function pointers handed out over IPC.  This
// translation unit hosts those bodies so they composite through the
// wasabi-compat GDI raster core (BitBlt/StretchBlt/FillRect/GetPixel)
// rather than a hand-coded substitute renderer.
//
// The theme comes from the genex bitmap, which WADlg_init fetches with
// SendMessage(hwndWinamp, WM_WA_IPC, 0, IPC_GET_GENSKINBITMAP).  qtamp's
// IPC handler returns a genex synthesised from the active skin's colours.

// wa_ipc.h (pulled by wa_dlg.h) typedefs `int intptr_t` under
// `#if _MSC_VER <= 1200`; with _MSC_VER undefined the preprocessor reads
// it as 0 and the (wrong, 32-bit) typedef fires + clashes with <stdint.h>.
// The ml_* plugin targets set _MSC_VER=1924 for the same reason; do it
// scoped to this TU (which pulls no Qt headers, so the compat-shim
// caveat about _MSC_VER + Qt doesn't apply here).
#define _MSC_VER 1300

#include "win32/windows.h"

#define WA_DLG_IMPLEMENT
#include "wa_dlg.h"
