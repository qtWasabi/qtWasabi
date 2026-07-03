// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// wadlg-render.cpp — render wa_dlg owner-draw chrome (the silver button)
// into a plain ARGB32 buffer, so the engine's MlHostWidget can draw the
// REAL Winamp button through the Qt-backed GDI raster core without pulling
// the win32 headers into engine code.  The genex bitmap (with the button
// cell painted by qtwasabi_make_genex) must already be installed via
// WADlg_init — MlHostWidget does that in installSkinGenex().

// wa_ipc.h (pulled by wa_dlg.h) does `typedef int intptr_t` under
// `#if _MSC_VER <= 1200`; define _MSC_VER (as wa-dlg-impl.cpp does) so that
// wrong 32-bit typedef is gated out and doesn't clash with <stdint.h>.
#define _MSC_VER 1300

#include "win32/windows.h"     // GDI + winuser: DRAWITEMSTRUCT, WM_DRAWITEM, ODT_*
#include "wa_dlg.h"            // WADlg_handleDialogMsgs decl (no WA_DLG_IMPLEMENT)
#include <cstdio>
#include <cstdlib>

// Draw a wa_dlg BS_OWNERDRAW button of size w x h (pressed = ODS_SELECTED)
// into `out`, a caller-owned w*h ARGB32 buffer (row-major, no padding).
extern "C" void qtwasabi_wadlg_button_argb(int w, int h, int pressed,
                                           unsigned int *out) {
    if (w <= 0 || h <= 0 || !out) return;

    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ oldBmp = SelectObject(dc, static_cast<HGDIOBJ>(bmp));

    // Isolate the owner-draw so wa_dlg's SelectObject(genex)/pen/brush
    // bindings don't leak into this DC beyond the call.
    SaveDC(dc);
    DRAWITEMSTRUCT di = {};
    di.CtlType   = ODT_BUTTON;
    di.hDC       = dc;
    di.rcItem.left = 0; di.rcItem.top = 0;
    di.rcItem.right = w; di.rcItem.bottom = h;
    di.itemState = pressed ? ODS_SELECTED : 0;
    WADlg_handleDialogMsgs(nullptr, WM_DRAWITEM, 0,
                           reinterpret_cast<LPARAM>(&di));
    RestoreDC(dc, -1);

    if (std::getenv("WASABIQT_TRACE_WADLG"))
        std::fprintf(stderr,
            "[wadlgbtn] %dx%d p=%d centre=0x%06lX corner=0x%06lX edge=0x%06lX\n",
            w, h, pressed, (unsigned long)GetPixel(dc, w / 2, h / 2),
            (unsigned long)GetPixel(dc, 4, 4), (unsigned long)GetPixel(dc, 0, 0));

    // Read the pixels back out of the QImage behind the HBITMAP.
    BITMAP bm = {};
    if (GetObjectW(bmp, sizeof(bm), &bm) && bm.bmBits) {
        const unsigned char *src = static_cast<const unsigned char *>(bm.bmBits);
        for (int y = 0; y < h; ++y) {
            const unsigned int *s = reinterpret_cast<const unsigned int *>(
                src + static_cast<size_t>(y) * bm.bmWidthBytes);
            unsigned int *d = out + static_cast<size_t>(y) * w;
            // The button face is opaque; wa_dlg's BitBlt copies RGB but the
            // genex source carries no alpha, so force it opaque.
            for (int x = 0; x < w; ++x) d[x] = s[x] | 0xFF000000u;
        }
    }

    SelectObject(dc, oldBmp);
    DeleteObject(static_cast<HGDIOBJ>(bmp));
    DeleteDC(dc);
}
