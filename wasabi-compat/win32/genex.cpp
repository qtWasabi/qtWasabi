// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// genex.cpp — synthesises the genex theme bitmap via the GDI raster
// core (CreateCompatibleBitmap + FillRect + SetPixel).  wa_dlg's
// WADlg_init then reads the 24 colours back with GetPixel — proving the
// whole skin-colour → genex → wa_dlg pipeline runs on our Qt-backed GDI.

#include "win32/windows.h"
#include "win32/genex.h"

extern "C" HBITMAP qtwasabi_make_genex(const COLORREF *colors24) {
    if (!colors24) return nullptr;
    // 112 wide covers the colour strip (x=48..94) + wa_dlg's defbgcol
    // probe at x=111; 30 tall leaves room for the 47x30 button cell a
    // later pass paints into x=0..46.
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateCompatibleBitmap(dc, 112, 30);
    SelectObject(dc, static_cast<HGDIOBJ>(bmp));

    // Fill with a magenta sentinel: wa_dlg reads defbgcol=GetPixel(111,0)
    // and treats any colour pixel == defbgcol as "unset" → default.  A
    // rare colour none of the 24 real ones equal keeps that from
    // mis-firing.
    RECT full = {0, 0, 112, 30};
    HBRUSH sentinel = CreateSolidBrush(RGB(255, 0, 255));
    FillRect(dc, &full, sentinel);
    DeleteObject(static_cast<HGDIOBJ>(sentinel));

    for (int x = 0; x < 24 /*WADLG_NUM_COLORS*/; ++x)
        SetPixel(dc, 48 + x * 2, 0, colors24[x]);

    DeleteDC(dc);   // unbinds; the bitmap stays alive in the registry
    return bmp;
}
