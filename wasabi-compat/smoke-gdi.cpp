// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// smoke-gdi.cpp — smoke for the GDI shim.  Compile +
// static_assert sanity for the GDI struct family + an HDC
// round-trip exercising SetTextColor/SetBkColor/BeginPaint/
// EndPaint state.
//

#include "win32/wingdi.h"
#include "win32/winuser.h"
#include "win32/genex.h"

#include "smoke-check.h"

// wa_dlg's real entry points (compiled in wa-dlg-impl.cpp).  Declared
// directly so this TU doesn't pull wa_dlg.h (+ its wa_ipc.h _MSC_VER
// guard).
extern "C" {
void WADlg_init(HWND);
int  WADlg_initted();
int  WADlg_getColor(int);
}

namespace qtWasabi {
namespace wasabi_compat {
namespace {

static_assert(SRCCOPY == 0x00CC0020, "BitBlt SRCCOPY constant");
static_assert(R2_COPYPEN == 13,      "SetROP2 default");
static_assert(TRANSPARENT == 1,      "SetBkMode TRANSPARENT");
static_assert(OPAQUE == 2,           "SetBkMode OPAQUE");

static_assert(offsetof(BITMAPINFOHEADER, biSize)   == 0, "BMI header layout");
static_assert(offsetof(BITMAPINFOHEADER, biWidth)  == 4, "BMI biWidth offset");
static_assert(offsetof(BITMAPINFOHEADER, biHeight) == 8, "BMI biHeight offset");
static_assert(offsetof(PAINTSTRUCT, hdc) == 0,           "PAINTSTRUCT.hdc first");

struct GdiSmoke {
    GdiSmoke() {
        HDC hdc = GetDC(nullptr);
        SMOKE_CHECK(hdc);

        // DC-state round-trip — colour + mode set/get.
        COLORREF prev_text = SetTextColor(hdc, RGB(255, 128, 64));
        SMOKE_CHECK(prev_text == 0);
        SMOKE_CHECK(GetTextColor(hdc) == RGB(255, 128, 64));

        COLORREF prev_bk = SetBkColor(hdc, RGB(16, 16, 16));
        SMOKE_CHECK(prev_bk == 0xFFFFFF);
        SMOKE_CHECK(GetBkColor(hdc) == RGB(16, 16, 16));

        int prev_mode = SetBkMode(hdc, TRANSPARENT);
        SMOKE_CHECK(prev_mode == OPAQUE);
        SMOKE_CHECK(GetBkMode(hdc) == TRANSPARENT);

        // SaveDC / RestoreDC round-trip — save, mutate, restore.
        int lvl = SaveDC(hdc);
        SMOKE_CHECK(lvl == 1);
        SetTextColor(hdc, RGB(1, 2, 3));
        SMOKE_CHECK(GetTextColor(hdc) == RGB(1, 2, 3));
        SMOKE_CHECK(RestoreDC(hdc, -1));
        SMOKE_CHECK(GetTextColor(hdc) == RGB(255, 128, 64));  // pre-save value

        // CreateCompatibleBitmap registers a HBITMAP backed by a
        // QImage — verify the handle is non-null and the
        // GetObject query doesn't crash on a zeroed buffer.
        HBITMAP bmp = CreateCompatibleBitmap(hdc, 64, 32);
        SMOKE_CHECK(bmp);
        BITMAP bm = {};
        GetObjectW(static_cast<HGDIOBJ>(static_cast<void *>(bmp)),
                    sizeof(bm), &bm);

        ReleaseDC(nullptr, hdc);

        // BeginPaint / EndPaint round-trip — fills the
        // PAINTSTRUCT and releases the DC on End.
        PAINTSTRUCT ps;
        HDC p_hdc = BeginPaint(nullptr, &ps);
        SMOKE_CHECK(p_hdc == ps.hdc);
        EndPaint(nullptr, &ps);

        // Raster core — real FillRect / BitBlt / GetPixel into a
        // bound bitmap.  If any regressed, qtamp aborts at boot with
        // the SMOKE_CHECK file:line.
        {
            const COLORREF RED  = RGB(255, 0, 0);
            const COLORREF BLUE = RGB(0, 0, 255);

            HDC dc = CreateCompatibleDC(nullptr);
            HBITMAP bm = CreateCompatibleBitmap(dc, 64, 64);
            SelectObject(dc, static_cast<HGDIOBJ>(static_cast<void *>(bm)));
            HBRUSH redBr = CreateSolidBrush(RED);
            RECT full = {0, 0, 64, 64};
            FillRect(dc, &full, redBr);
            SMOKE_CHECK(GetPixel(dc, 10, 10) == RED);   // fill landed
            SMOKE_CHECK(GetPixel(dc, 63, 63) == RED);   // covers full rect

            // Second surface filled blue, BitBlt a 16x16 corner onto dc.
            HDC sdc = CreateCompatibleDC(nullptr);
            HBITMAP sbm = CreateCompatibleBitmap(sdc, 64, 64);
            SelectObject(sdc, static_cast<HGDIOBJ>(static_cast<void *>(sbm)));
            HBRUSH blueBr = CreateSolidBrush(BLUE);
            FillRect(sdc, &full, blueBr);
            BitBlt(dc, 0, 0, 16, 16, sdc, 0, 0, SRCCOPY);
            SMOKE_CHECK(GetPixel(dc, 8, 8) == BLUE);    // copied region
            SMOKE_CHECK(GetPixel(dc, 32, 32) == RED);   // outside stays red

            SMOKE_CHECK(GetStockObject(BLACK_BRUSH));   // stock real+non-null

            DeleteObject(static_cast<HGDIOBJ>(static_cast<void *>(redBr)));
            DeleteObject(static_cast<HGDIOBJ>(static_cast<void *>(blueBr)));
            DeleteDC(dc);
            DeleteDC(sdc);
        }

        // The real wa_dlg colour pipeline (not a stub) end-to-end on
        // the raster core: synthesise a genex from 24 known colours,
        // install it, run WADlg_init (which SendMessage()s for the genex
        // then GetPixel()s 48+2x,0), and assert it read them back.
        {
            COLORREF want[24];
            for (int x = 0; x < 24; ++x)
                want[x] = RGB(x * 9 + 1, x * 5 + 2, 100 + x);  // distinct, non-magenta
            HBITMAP genex = qtwasabi_make_genex(want);
            SMOKE_CHECK(genex);
            qtwasabi_set_genskin_bitmap(genex);
            WADlg_init(reinterpret_cast<HWND>(static_cast<void *>(genex)));  // hwnd ignored by our IPC
            SMOKE_CHECK(WADlg_initted());
            for (int x = 0; x < 24; ++x)
                SMOKE_CHECK(WADlg_getColor(x) == (int)want[x]);
        }
    }
};
static GdiSmoke s_smoke;

}  // anonymous
}  // namespace wasabi_compat
}  // namespace qtWasabi
