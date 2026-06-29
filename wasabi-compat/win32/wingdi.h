// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#ifndef _WINGDI_H
#define _WINGDI_H
//
// wingdi.h — Win32 GDI API surface.  Ships the constants + function
// declarations gen_ml + ml_* code references; the implementation in
// wingdi-stubs.cpp is no-op-friendly because most real rendering paths
// land in NM_CUSTOMDRAW, which our commctrl host routes directly to
// QPainter.  The GDI calls here are functional for the few code paths
// that go through them (background fills, simple text draws) via the
// HDC → QPainter mapping.
//

#include "basetsd.h"
#include "windef.h"

#ifdef __cplusplus
extern "C" {
#endif

// HGDIOBJ + the GDI object handles (HBITMAP/HBRUSH/HPEN/HFONT/HRGN) are
// void* typedefs in windef.h (real Win32 non-strict) so Winamp's wa_dlg /
// gen_ml can pass any of them into SelectObject(HDC, HGDIOBJ) /
// DeleteObject(HGDIOBJ).  No DECLARE_HANDLE here — windef.h owns them.

// ── Stock objects (GetStockObject indices) ─────────────────────
#define WHITE_BRUSH            0
#define LTGRAY_BRUSH           1
#define GRAY_BRUSH             2
#define DKGRAY_BRUSH           3
#define BLACK_BRUSH            4
#define NULL_BRUSH             5
#define HOLLOW_BRUSH           NULL_BRUSH
#define WHITE_PEN              6
#define BLACK_PEN              7
#define NULL_PEN               8
#define OEM_FIXED_FONT         10
#define ANSI_FIXED_FONT        11
#define ANSI_VAR_FONT          12
#define SYSTEM_FONT            13
#define DEVICE_DEFAULT_FONT    14
#define DEFAULT_PALETTE        15
#define SYSTEM_FIXED_FONT      16
#define DEFAULT_GUI_FONT       17

// ── Background modes (SetBkMode) ───────────────────────────────
#define TRANSPARENT            1
#define OPAQUE                 2

// ── Raster ops (SetROP2 / BitBlt) ──────────────────────────────
#define SRCCOPY                0x00CC0020
#define SRCAND                 0x008800C6
#define SRCPAINT               0x00EE0086
#define SRCINVERT              0x00660046
#define SRCERASE               0x00440328
#define NOTSRCCOPY             0x00330008
#define NOTSRCERASE            0x001100A6
#define MERGECOPY              0x00C000CA
#define MERGEPAINT             0x00BB0226
#define PATCOPY                0x00F00021
#define PATPAINT               0x00FB0A09
#define PATINVERT              0x005A0049
#define DSTINVERT              0x00550009
#define BLACKNESS              0x00000042
#define WHITENESS              0x00FF0062

#define R2_BLACK               1
#define R2_NOTMERGEPEN         2
#define R2_MASKNOTPEN          3
#define R2_NOTCOPYPEN          4
#define R2_MASKPENNOT          5
#define R2_NOT                 6
#define R2_XORPEN              7
#define R2_NOTMASKPEN          8
#define R2_MASKPEN             9
#define R2_NOTXORPEN           10
#define R2_NOP                 11
#define R2_MERGENOTPEN         12
#define R2_COPYPEN             13
#define R2_MERGEPENNOT         14
#define R2_MERGEPEN            15
#define R2_WHITE               16

// ── DrawText format flags ──────────────────────────────────────
#define DT_TOP                 0x00000000
#define DT_LEFT                0x00000000
#define DT_CENTER              0x00000001
#define DT_RIGHT               0x00000002
#define DT_VCENTER             0x00000004
#define DT_BOTTOM              0x00000008
#define DT_WORDBREAK           0x00000010
#define DT_SINGLELINE          0x00000020
#define DT_EXPANDTABS          0x00000040
#define DT_TABSTOP             0x00000080
#define DT_NOCLIP              0x00000100
#define DT_EXTERNALLEADING     0x00000200
#define DT_CALCRECT            0x00000400
#define DT_NOPREFIX            0x00000800
#define DT_INTERNAL            0x00001000
#define DT_EDITCONTROL         0x00002000
#define DT_PATH_ELLIPSIS       0x00004000
#define DT_END_ELLIPSIS        0x00008000
#define DT_MODIFYSTRING        0x00010000
#define DT_RTLREADING          0x00020000
#define DT_WORD_ELLIPSIS       0x00040000

// ── BITMAP / DIB structs (subset) ──────────────────────────────
typedef struct tagBITMAP {
    LONG    bmType;
    LONG    bmWidth;
    LONG    bmHeight;
    LONG    bmWidthBytes;
    WORD    bmPlanes;
    WORD    bmBitsPixel;
    LPVOID  bmBits;
} BITMAP, *PBITMAP, *LPBITMAP;

typedef struct tagBITMAPINFOHEADER {
    DWORD biSize;
    LONG  biWidth;
    LONG  biHeight;
    WORD  biPlanes;
    WORD  biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG  biXPelsPerMeter;
    LONG  biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER, *LPBITMAPINFOHEADER;

typedef struct tagRGBQUAD {
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbReserved;
} RGBQUAD;

typedef struct tagBITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD          bmiColors[1];
} BITMAPINFO, *PBITMAPINFO, *LPBITMAPINFO;

#define BI_RGB                 0
#define BI_RLE8                1
#define BI_RLE4                2
#define BI_BITFIELDS           3
#define BI_JPEG                4
#define BI_PNG                 5

#define DIB_RGB_COLORS         0
#define DIB_PAL_COLORS         1

// ── DC management ──────────────────────────────────────────────
HDC      WINAPI GetDC                 (HWND);
HDC      WINAPI GetWindowDC           (HWND);
int      WINAPI ReleaseDC             (HWND, HDC);
HDC      WINAPI CreateCompatibleDC    (HDC);
BOOL     WINAPI DeleteDC              (HDC);
int      WINAPI SaveDC                (HDC);
BOOL     WINAPI RestoreDC             (HDC, int);
HBITMAP  WINAPI CreateCompatibleBitmap(HDC, int, int);
HBITMAP  WINAPI CreateDIBSection      (HDC, const BITMAPINFO *, UINT, void **, HANDLE, DWORD);

// ── wa_dlg.h support: pen styles, ROP/stretch modes, brush style,
// CLR_INVALID, LOGBRUSH, region ops ────────────────────────────
#ifndef PS_SOLID
#define PS_SOLID      0
#define PS_NULL       5
#endif
#ifndef BS_SOLID
#define BS_SOLID      0
#endif
#ifndef CLR_INVALID
#define CLR_INVALID   0xFFFFFFFF
#endif
// SetStretchBltMode modes (we always nearest-neighbour, but the calls
// must compile + return a sane previous mode).
#ifndef BLACKONWHITE
#define BLACKONWHITE  1
#define WHITEONBLACK  2
#define COLORONCOLOR  3
#define HALFTONE      4
#endif
// CombineRgn modes.
#ifndef RGN_AND
#define RGN_AND   1
#define RGN_OR    2
#define RGN_XOR   3
#define RGN_DIFF  4
#define RGN_COPY  5
#endif
// CombineRgn / region-complexity return codes.
#ifndef NULLREGION
#define NULLREGION    1
#define SIMPLEREGION  2
#define COMPLEXREGION 3
#endif

typedef struct tagLOGBRUSH {
    UINT      lbStyle;
    COLORREF  lbColor;
    ULONG_PTR lbHatch;
} LOGBRUSH, *PLOGBRUSH, *LPLOGBRUSH;

int      WINAPI SetStretchBltMode     (HDC, int);
COLORREF WINAPI GetNearestColor       (HDC, COLORREF);
HBRUSH   WINAPI CreateBrushIndirect   (const LOGBRUSH *);
HRGN     WINAPI CreateRectRgn         (int, int, int, int);
HRGN     WINAPI CreateRectRgnIndirect (const RECT *);
int      WINAPI CombineRgn            (HRGN, HRGN, HRGN, int);
int      WINAPI FillRgn               (HDC, HRGN, HBRUSH);

// ── Object management ─────────────────────────────────────────
HBRUSH   WINAPI CreateSolidBrush      (COLORREF);
HPEN     WINAPI CreatePen             (int style, int width, COLORREF);
HFONT    WINAPI CreateFontW           (int, int, int, int, int, DWORD, DWORD, DWORD,
                                          DWORD, DWORD, DWORD, DWORD, DWORD,
                                          LPCWSTR faceName);
HGDIOBJ  WINAPI GetStockObject        (int);
HGDIOBJ  WINAPI SelectObject          (HDC, HGDIOBJ);
BOOL     WINAPI DeleteObject          (HGDIOBJ);
int      WINAPI GetObjectW            (HGDIOBJ, int, LPVOID);

// ── Drawing ────────────────────────────────────────────────────
BOOL     WINAPI BitBlt                (HDC dst, int x, int y, int w, int h,
                                          HDC src, int sx, int sy, DWORD rop);
BOOL     WINAPI StretchBlt            (HDC dst, int x, int y, int w, int h,
                                          HDC src, int sx, int sy, int sw, int sh, DWORD rop);
BOOL     WINAPI Rectangle             (HDC, int, int, int, int);
int      WINAPI FillRect              (HDC, const RECT *, HBRUSH);
int      WINAPI FrameRect             (HDC, const RECT *, HBRUSH);
BOOL     WINAPI MoveToEx              (HDC, int, int, LPPOINT);
BOOL     WINAPI LineTo                (HDC, int, int);
COLORREF WINAPI SetPixel              (HDC, int, int, COLORREF);
COLORREF WINAPI GetPixel              (HDC, int, int);
COLORREF WINAPI SetTextColor          (HDC, COLORREF);
COLORREF WINAPI GetTextColor          (HDC);
COLORREF WINAPI SetBkColor            (HDC, COLORREF);
COLORREF WINAPI GetBkColor            (HDC);
int      WINAPI SetBkMode             (HDC, int);
int      WINAPI GetBkMode             (HDC);
int      WINAPI SetROP2               (HDC, int);
int      WINAPI GetROP2               (HDC);
BOOL     WINAPI TextOutW              (HDC, int, int, LPCWSTR, int);
BOOL     WINAPI ExtTextOutW           (HDC, int, int, UINT, const RECT *,
                                          LPCWSTR, UINT, const INT *);
int      WINAPI DrawTextW             (HDC, LPCWSTR, int, LPRECT, UINT);
int      WINAPI DrawTextA             (HDC, LPCSTR, int, LPRECT, UINT);
BOOL     WINAPI GetTextExtentPoint32W (HDC, LPCWSTR, int, LPSIZE);
DWORD    WINAPI GetFontLanguageInfo   (HDC);
#ifndef GCP_REORDER
#define GCP_REORDER 0x0002
#endif

#define ExtTextOut                ExtTextOutW
#define TextOut                   TextOutW
#define DrawText                  DrawTextW
#define GetTextExtentPoint32       GetTextExtentPoint32W

// ── BeginPaint / EndPaint ──────────────────────────────────────
typedef struct tagPAINTSTRUCT {
    HDC     hdc;
    BOOL    fErase;
    RECT    rcPaint;
    BOOL    fRestore;
    BOOL    fIncUpdate;
    BYTE    rgbReserved[32];
} PAINTSTRUCT, *LPPAINTSTRUCT;

HDC      WINAPI BeginPaint            (HWND, LPPAINTSTRUCT);
BOOL     WINAPI EndPaint              (HWND, const PAINTSTRUCT *);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // _WINGDI_H
