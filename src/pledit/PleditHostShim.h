// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// PleditHostShim.h — the data seam between Winamp's real draw_pe.cpp
// (which reads the playlist via global PlayList_* functions) and qtamp's
// Host.  The shim TU (PleditHostShim.cpp) defines the PlayList_* +
// draw.h globals/helpers draw_pe links against, routing the data calls
// through this abstract source.  The renderer installs a source backed
// by qtWasabi::Host each paint.  Keeps the shim free of Qt/Host headers
// (it pulls the Winamp <windows.h> chain, which mixes badly with Qt).

#ifndef QTWASABI_PLEDIT_HOST_SHIM_H
#define QTWASABI_PLEDIT_HOST_SHIM_H

namespace qtWasabi {

struct PleditDataSource {
    virtual ~PleditDataSource() = default;
    virtual int  rowCount()                            = 0;
    virtual void rowText(int row, wchar_t *out, int cap) = 0;  // display title
    virtual int  rowDurationSec(int row)               = 0;
    virtual int  currentRow()                          = 0;    // playing index
};

// Installed by PleditHostRenderer before each draw_pl2 call.
void pleditSetSource(PleditDataSource *src);
// Set the playlist palette from the ACTIVE SKIN (each arg 0xRRGGBB).
// draw_pe otherwise uses the hardcoded classic-Winamp green; this lets
// Bento render its grey `color.display` rows (green only appears when a
// classic colour scheme's `wasabi.list.text` actually resolves green).
void pleditSetColors(unsigned normalRGB, unsigned normalBgRGB,
                     unsigned currentRGB, unsigned selectedBgRGB);
// Scroll offset (== draw_pe's pledit_disp_offs); the renderer owns it.
void pleditSetScroll(int topRow);
int  pleditGetScroll();
// Holder rect each paint → draw_pe's config_pe_width / config_pe_height.
void pleditSetSize(int w, int h);
// draw_pe's pe_fontheight (glyph cell height).
void pleditSetFontHeight(int h);
// Row pitch, decoupled from the glyph size (draw_pe ties both to
// pe_fontheight; our GDI-compat font renders smaller glyphs per lfHeight,
// so the pitch is set separately to match the reference's ~16px rows).
void pleditSetRowHeight(int h);
// Switch draw_pe into "list-only" mode for a buffer that is just the row
// area (the Wasabi frame owns the titlebar/menubar/button bar): `top` is
// the y of row 0, `reserve` the height to deduct from the row count, and
// `fillBg` fills the whole buffer with the list background first.  Also
// suppresses draw_pe's own bottom time/status strip.
void pleditSetListGeometry(int top, int reserve, int fillBg);

// Render the playlist (bg + rows) via the REAL draw_pe into a top-down
// ARGB32 buffer.  The Qt-side renderer wraps it as a QImage + drawImage.
// No win32/Qt types in the signature — the clean Qt↔win32 seam.
void pleditRenderToArgb(unsigned char *dst, int w, int h,
                        int dstStride, int scroll);

}  // namespace qtWasabi

#endif  // QTWASABI_PLEDIT_HOST_SHIM_H
