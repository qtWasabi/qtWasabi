// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// genex.h — synthesise + install the "genex" theme bitmap that Winamp's
// wa_dlg reads (WADlg_init → SendMessage(WM_WA_IPC, IPC_GET_GENSKINBITMAP)
// → GetPixel(48+2*idx, 0) for the 24 WADLG_* colours).  Modern skins
// (Bento) ship no genex.bmp, so we build one from the active skin's
// colours and answer the IPC with it.

#ifndef QTWASABI_GENEX_H
#define QTWASABI_GENEX_H

#include "win32/windef.h"   // HBITMAP, COLORREF

#ifdef __cplusplus
extern "C" {
#endif

// Build a genex HBITMAP from the 24 WADLG_* colours (COLORREF, index
// order matches the WADLG_* enum in wa_dlg.h).  Colours land at
// (48+2*idx, 0); the rest is filled with a magenta sentinel so wa_dlg's
// defbgcol(111,0) never collides with a real colour.  Registry-owned.
HBITMAP qtwasabi_make_genex(const COLORREF *colors24);

// Install the genex the IPC_GET_GENSKINBITMAP handler hands back.
void    qtwasabi_set_genskin_bitmap(HBITMAP genex);

#ifdef __cplusplus
}
#endif

#endif  // QTWASABI_GENEX_H
