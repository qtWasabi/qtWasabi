// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// PleditHostShim.cpp — defines the globals + helper functions Winamp's
// real draw_pe.cpp links against, so its draw_pl2/draw_pe_song row
// renderer runs inside qtamp.  PlayList_* route through the abstract
// PleditDataSource (renderer-installed, Host-backed).  Stage 1: GDI text
// path (config_bifont=0), stub skin bitmaps, classic pledit colours.
//
// This TU pulls the Winamp <windows.h> shim chain (NOT Qt/Host headers —
// they mix badly), matching how draw_pe.cpp itself compiles.

#include <windows.h>
#include <cstring>
#include "api.h"                 // staged: PlaylistTextFeed decl
#include "WADrawDC.h"            // Winamp's window-DC RAII wrapper (draw_pe)
#include "PleditHostShim.h"

// ── The renderer-installed data source + scroll/size state ─────────
namespace {
qtWasabi::PleditDataSource *g_src = nullptr;
}
namespace qtWasabi {
void pleditSetSource(PleditDataSource *s) { g_src = s; }
void pleditSetScroll(int top);            // fwd (defined after pledit_disp_offs)
int  pleditGetScroll();
void pleditSetSize(int w, int h);
void pleditSetFontHeight(int h);
}

// ── draw.h externs (extern "C" — match draw.h's linkage) ───────────
extern "C" {
COLORREF mfont_bgcolor = 0x000000, mfont_fgcolor = 0xFFFFFF;
unsigned char *specData = nullptr;
// disable_skin_borders=1: this is the EMBEDDED classic playlist (real
// Winamp sets it via IPC_SETDRAWBORDERS for a modern-skin-hosted pledit).
// It tells draw_pe to render ROWS ONLY — Bento draws the bevel, the
// button bar, the search header, the scrollbar and the cover as its own
// skin widgets, so draw_pe must not paint classic chrome over them.
int sa_safe = 0, disable_skin_borders = 1, mfont_height = 12;
int g_has_deleted_current = 0;
volatile int draw_initted = 1;

HFONT font = nullptr, mfont = nullptr, shadefont = nullptr, osdFontText = nullptr;
HBRUSH selbrush = nullptr, normbrush = nullptr, mfont_bgbrush = nullptr;
HBITMAP fontBM = nullptr, embedBM = nullptr, panBM = nullptr,
        shufflerepeatBM = nullptr, tbBM = nullptr, cbuttonsBM = nullptr,
        volBM = nullptr, mainBM2 = nullptr, oldmainBM2 = nullptr,
        numbersBM = nullptr, numbersBM_ex = nullptr, playpauseBM = nullptr,
        posbarBM = nullptr, monostereoBM = nullptr;
HDC bmDC = nullptr, mainDC = nullptr, specDC = nullptr, mainDC2 = nullptr;
CRITICAL_SECTION g_mainwndcs, g_srcdccs;
int titlebar_font_offsets[26]    = {0};
int titlebar_font_widths[26]     = {0};
int titlebar_font_num_offsets[12]= {0};
int titlebar_font_num_widths[12] = {0};
int titlebar_font_unknown_width  = 5;
int updateen = 1;

// draw.h helpers — Stage 1 stubs (GDI text path; no skin-bitmap blits).
void _setSrcBM(HBITMAP hbm) { if (bmDC && hbm) SelectObject(bmDC, (HGDIOBJ)hbm); }
void update_area(int, int, int, int) {}
HBITMAP draw_LBitmap(LPCTSTR, const wchar_t *) { return nullptr; }  // Stage 2 loads real skin bmp
HDC draw_GetWindowDC(HWND) { return nullptr; }
int draw_ReleaseDC(HWND, HDC) { return 0; }
void getXYfromChar(wchar_t, int *x, int *y) { if (x) *x = 0; if (y) *y = 0; }
void do_palmode(HDC) {}

// winbase atom decls — playlist windows key props by atom.
ATOM GlobalAddAtomW(LPCWSTR) { static ATOM n = 0xC000; return ++n; }
ATOM GlobalDeleteAtom(ATOM)  { return 0; }
}  // extern "C"

// ── Main.h globals (plain C++ linkage) ─────────────────────────────
int config_pe_width = 200, config_pe_height = 300;
int config_pe_direction = 0, config_bifont = 0, config_hilite = 0;
int pe_fontheight = 12;
// Row pitch, decoupled from the glyph size.  Classic draw_pe uses
// pe_fontheight for BOTH, but Win32's lfHeight is a CELL height (glyph +
// internal leading) while our GDI-compat font renders a given lfHeight as
// a smaller glyph than Windows does.  The reference Playlist Editor has
// ~16px rows with ~9px glyphs; matching the glyph size needs lfHeight≈12
// here, so the row pitch is carried separately.  Defaults to pe_fontheight.
int pe_rowheight = 12;
int pledit_disp_offs = 0;
HWND hPLWindow = nullptr;
// List-only render geometry.  draw_pe is written for the FULL playlist
// window: it starts row 0 at y=22 (under the classic titlebar) and
// deducts ~60px of titlebar+statusbar chrome from the visible row count.
// qtamp hands draw_pe a LIST-ONLY buffer — the Wasabi frame paints the
// titlebar / menubar / button bar separately — so those offsets must be
// neutralised, the list background filled (the frame doesn't own it on
// Winamp Modern), and draw_pe's own bottom time/status strip suppressed.
// Defaults keep the classic full-window behaviour; pleditSetListGeometry
// switches to list-only.
int pe_list_top     = 22;
int pe_list_reserve = 60;
int pe_fill_bg      = 0;
int pe_list_only    = 0;
// Classic pledit palette (Normal green / Current white / NormalBG black /
// SelectedBG blue / mb fg/bg).  Stage 3 swaps in the live wa_dlg colours.
COLORREF Skin_PLColors[6] = {
    0x0000FF00 /*Normal*/, 0x00FFFFFF /*Current*/, 0x00000000 /*NormalBG*/,
    0x00C60000 /*SelectedBG*/, 0x0000FF00 /*mbFG*/, 0x00000000 /*mbBG*/
};

// ── Main.h symbols — extern "C" (Main.h wraps its whole API in it) ──
extern "C" {
// PlayList_* route through the renderer's Host-backed source.
int PlayList_getlength()                 { return g_src ? g_src->rowCount() : 0; }
int PlayList_getitem_pl(int pos, wchar_t *buf) {
    if (buf) { if (g_src) g_src->rowText(pos, buf, 255); else buf[0] = 0; }
    return 0;
}
int PlayList_getsonglength(int pos)      { return g_src ? g_src->rowDurationSec(pos) : 0; }
int PlayList_getselect(int /*pos*/)      { return 0; }   // per-row selection: Stage 3
int PlayList_getPosition()               { return g_src ? g_src->currentRow() : -1; }
int in_getlength()                       { return 0; }   // current decoder length (ms)

// getStringW — Stage 1: empty string (titlebar "no file" text etc.).
wchar_t *getStringW(unsigned int /*uID*/, wchar_t *str, size_t maxlen) {
    if (str && maxlen) str[0] = 0;
    return str;
}

// Main.h globals referenced by draw_pe (extern "C").
HWND hMainWindow = nullptr;     // Main.h: extern HWND hMainWindow, …
wchar_t FileTitle[2048] = {0};
}  // extern "C"

// ── Win32 fns draw_pe references that the shims only declared ──────
// Defined plain after <windows.h> so each inherits the header's
// (extern "C") linkage automatically.  Only the ones that linked
// undefined (Enter/Leave critsec are no-ops — single-threaded render).
void WINAPI EnterCriticalSection(LPCRITICAL_SECTION)      {}
void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION)      {}
HWND   WINAPI GetForegroundWindow(void)        { return nullptr; }
LPWSTR WINAPI CharUpperW(LPWSTR s)             { return s; }
// draw_pe_song draws the per-row time/duration (" 5:37 ") via DrawTextA.
// It was a no-op, so durations never rendered (and DT_CALCRECT returned
// 0, so the time column reserved no space).  Forward to the real
// DrawTextW — the time strings are pure ASCII so a widening copy is safe.
int    WINAPI DrawTextA(HDC hdc, LPCSTR str, int n, LPRECT rc, UINT fmt) {
    if (!str) return 0;
    if (n < 0) n = (int)std::strlen(str);
    wchar_t buf[256];
    int i = 0;
    for (; i < n && i < 255; ++i) buf[i] = (wchar_t)(unsigned char)str[i];
    buf[i] = 0;
    return DrawTextW(hdc, buf, i, rc, fmt);
}
DWORD  WINAPI GetFontLanguageInfo(HDC)         { return 0; }

// ── WADrawDC — Winamp's window-DC RAII wrapper (draw_paint_pe uses it;
// not on our render path, but referenced, so provide trivial bodies). ─
WADrawDC::WADrawDC(HWND h)          : hdc(nullptr), hwnd(h) {}
WADrawDC::WADrawDC(HDC d, HWND h)   : hdc(d), hwnd(h) {}
WADrawDC::~WADrawDC()               {}
WADrawDC::operator HDC()            { return hdc; }

// ── playlistTextFeed instance (staged api.h declared the extern) ───
PlaylistTextFeed g_playlistTextFeed_inst;
PlaylistTextFeed *playlistTextFeed = &g_playlistTextFeed_inst;
void PlaylistTextFeed::UpdateText(const wchar_t *, int) {}

// ── scroll / size setters (defined here where the globals live) ────
namespace qtWasabi {
void pleditSetScroll(int top)       { pledit_disp_offs = top; }
int  pleditGetScroll()              { return pledit_disp_offs; }
void pleditSetSize(int w, int h)    { config_pe_width = w; config_pe_height = h; }
void pleditSetListGeometry(int top, int reserve, int fillBg) {
    pe_list_top     = top;
    pe_list_reserve = reserve;
    pe_fill_bg      = fillBg ? 1 : 0;
    pe_list_only    = 1;   // suppress draw_pe's own bottom time/status strip
}
void pleditSetRowHeight(int h) { if (h > 0) pe_rowheight = h; }
void pleditSetFontHeight(int h) {
    if (h > 0) pe_fontheight = h;
    // Create the pledit row fonts at pe_fontheight so draw_pe's
    // SelectObject(hdc, font/mfont) binds a REAL font.  Without this they
    // stay null → DrawTextW falls back to fontFromDc's hardcoded 11px
    // DejaVu, so every playlist row title + the time column rendered at the
    // wrong size and typeface (vs the reference's Arial ~12).  Classic
    // Winamp pledit font = Arial @ pe_fontheight; general for any draw_pe
    // skin, no skin ids.  Create-if-null (pe_fontheight is fixed per skin).
    if (!font)
        font = CreateFontW(pe_fontheight, 0, 0, 0, 400, 0, 0, 0,
                           0, 0, 0, 0, 0, L"Arial");
    if (!mfont)
        mfont = CreateFontW(mfont_height > 0 ? mfont_height : pe_fontheight,
                            0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, L"Arial");
}
void pleditSetColors(unsigned normalRGB, unsigned normalBgRGB,
                     unsigned currentRGB, unsigned selectedBgRGB) {
    auto toRef = [](unsigned rgb) -> COLORREF {
        return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    };
    Skin_PLColors[0] = toRef(normalRGB);      // normal text
    Skin_PLColors[1] = toRef(currentRGB);     // current/playing text
    Skin_PLColors[2] = toRef(normalBgRGB);    // normal background
    Skin_PLColors[3] = toRef(selectedBgRGB);  // selected-row background
    Skin_PLColors[4] = toRef(normalRGB);      // minibrowser fg
    Skin_PLColors[5] = toRef(normalBgRGB);    // minibrowser bg
    // Row brushes are lazily built from these in pleditRenderToArgb; drop
    // them so the next render rebuilds with the new colours.
    if (normbrush) { DeleteObject((HGDIOBJ)normbrush); normbrush = nullptr; }
    if (selbrush)  { DeleteObject((HGDIOBJ)selbrush);  selbrush  = nullptr; }
}
}

// ── win32-side render bridge: run the REAL draw_pe into an ARGB buffer
// the Qt-side renderer wraps as a QImage.  Decoupled from Qt entirely. ──
extern "C" void qtamp_pe_render(HWND, HDC, int, int);  // staged draw_pe.cpp

namespace qtWasabi {
void pleditRenderToArgb(unsigned char *dst, int w, int h,
                        int dstStride, int scroll) {
    if (!dst || w <= 0 || h <= 0) return;
    pledit_disp_offs = scroll;
    config_pe_width = w;
    config_pe_height = h;
    // Debug knobs for tuning the list geometry against a reference without
    // a rebuild (WASABIQT_PE_ROWH / _TOP / _RESERVE).
    if (const char *e = std::getenv("WASABIQT_PE_ROWH"))    { int v=std::atoi(e); if (v>0) pe_rowheight=v; }
    if (const char *e = std::getenv("WASABIQT_PE_TOP"))     pe_list_top=std::atoi(e);
    if (const char *e = std::getenv("WASABIQT_PE_RESERVE")) pe_list_reserve=std::atoi(e);

    // Per-row background brushes draw_pe_song fills each row with (normal
    // rows = Skin_PLColors[2], the current/selected row = Skin_PLColors[3]).
    // Created once; without them rows are transparent text and the current
    // track gets no highlight bar.
    if (!normbrush) normbrush = CreateSolidBrush(Skin_PLColors[2]);
    if (!selbrush)  selbrush  = CreateSolidBrush(Skin_PLColors[3]);

    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi;
    std::memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize     = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth    = w;
    bi.bmiHeader.biHeight    = -h;   // top-down (row 0 = top, like QImage)
    bi.bmiHeader.biPlanes    = 1;
    bi.bmiHeader.biBitCount  = 32;
    void *bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, 0, &bits, nullptr, 0);
    SelectObject(dc, (HGDIOBJ)bmp);

    // Bento paints its own pledit.background.* 9-grid bevel behind this
    // holder, so it wants the buffer left transparent (draw_pe_song fills
    // each row's own rect).  Winamp Modern's standalone Playlist Editor
    // has NO such backing widget — the list area would show the desktop
    // through the gaps between rows.  When the holder asks for a fill
    // (pe_fill_bg), paint the whole buffer with the list background first
    // so the area reads as one solid list like the reference.
    if (pe_fill_bg) {
        RECT full = {0, 0, w, h};
        FillRect(dc, &full, normbrush);
    }
    qtamp_pe_render(nullptr, dc, w, h);   // rows via real draw_pe (list-only)

    // Read the CURRENT backing bits (QPainter draws may have detached the
    // QImage since CreateDIBSection handed back `bits`).
    BITMAP bm;
    std::memset(&bm, 0, sizeof(bm));
    GetObjectW((HGDIOBJ)bmp, sizeof(bm), &bm);
    unsigned char *cur = bm.bmBits ? static_cast<unsigned char *>(bm.bmBits)
                                   : static_cast<unsigned char *>(bits);
    const int srcStride = bm.bmWidthBytes ? bm.bmWidthBytes : w * 4;
    if (cur) {
        for (int y = 0; y < h; ++y)
            std::memcpy(dst + y * dstStride, cur + y * srcStride, w * 4);
    }
    DeleteObject((HGDIOBJ)bmp);
    DeleteDC(dc);
}
}  // namespace qtWasabi
