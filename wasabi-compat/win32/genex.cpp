// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// genex.cpp — synthesises the genex theme bitmap via the GDI raster
// core (CreateCompatibleBitmap + FillRect + SetPixel).  wa_dlg's
// WADlg_init then reads the 24 colours back with GetPixel — proving the
// whole skin-colour → genex → wa_dlg pipeline runs on our Qt-backed GDI.

#include "win32/windows.h"
#include "win32/genex.h"
#include <cstdio>
#include <cstdlib>

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

    // ── 47x30 silver button cell (x=0..46) ──────────────────────────
    // wa_dlg 9-slices this out of the genex (wa_dlg.h): the outer 4 px on
    // each side are the non-stretched corners/edges, the inner region is
    // stretched to the button size.  Normal state lives in rows 0..14,
    // pressed in rows 15..29.  It is a classic Win9x-style raised/sunken
    // silver button (fixed silver so it reads as a button on any skin;
    // the surrounding ML chrome takes the skin's WADLG colours instead).
    auto fill = [&](int x, int y, int w, int h, COLORREF c) {
        RECT r = {x, y, x + w, y + h};
        HBRUSH b = CreateSolidBrush(c);
        FillRect(dc, &r, b);
        DeleteObject(static_cast<HGDIOBJ>(b));
    };
    const COLORREF kFace  = RGB(206, 206, 210);
    const COLORREF kFaceP = RGB(188, 188, 192);   // pressed face (slightly darker)
    const COLORREF kLight = RGB(244, 244, 247);    // highlight bevel
    const COLORREF kDark  = RGB(120, 120, 124);    // shadow bevel
    const COLORREF kEdge  = RGB(72, 72, 76);       // outer 1px frame
    for (int state = 0; state < 2; ++state) {
        const int  y0      = state * 15;
        const bool pressed = state == 1;
        const COLORREF face = pressed ? kFaceP : kFace;
        const COLORREF tl   = pressed ? kDark  : kLight;   // top/left bevel
        const COLORREF br   = pressed ? kLight : kDark;    // bottom/right bevel
        fill(0,  y0,      47, 15, kEdge);   // outer frame
        fill(1,  y0 + 1,  45, 13, face);    // inner face
        fill(1,  y0 + 1,  45, 1,  tl);      // inner top bevel
        fill(1,  y0 + 1,  1,  13, tl);      // inner left bevel
        fill(1,  y0 + 13, 45, 1,  br);      // inner bottom bevel
        fill(45, y0 + 1,  1,  13, br);      // inner right bevel
    }

    if (std::getenv("WASABIQT_TRACE_WADLG")) {
        std::fprintf(stderr,
            "[genex] btn N face(23,7)=0x%06lX tl(1,1)=0x%06lX br(45,13)=0x%06lX  "
            "P face(23,22)=0x%06lX tl(1,16)=0x%06lX\n",
            (unsigned long)GetPixel(dc, 23, 7),  (unsigned long)GetPixel(dc, 1, 1),
            (unsigned long)GetPixel(dc, 45, 13), (unsigned long)GetPixel(dc, 23, 22),
            (unsigned long)GetPixel(dc, 1, 16));
    }

    DeleteDC(dc);   // unbinds; the bitmap stays alive in the registry
    return bmp;
}
