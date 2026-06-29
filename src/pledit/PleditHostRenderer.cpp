// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// PleditHostRenderer.cpp — the Qt side of the real in-player playlist.
// A HolderRenderer registered for the Playlist GUID {45F3F7C1-…}; it
// implements PleditDataSource over qtWasabi::Host and drives Winamp's
// REAL draw_pe (via pleditRenderToArgb) into a QImage, then drawImage's
// it into the windowholder.  Retires the hand-built PlaylistPro.
//
// This TU is Qt-only — the win32/draw_pe side lives behind the clean
// PleditHostShim seam (no <windows.h> here).

#include "PleditHostRenderer.h"
#include "PleditHostShim.h"

#include <qtWasabi/WindowHolderRegistry.h>
#include <qtWasabi/PaintCtx.h>
#include <qtWasabi/Host.h>
#include <qtWasabi/ColorRegistry.h>
#include <qtWasabi/BitmapRegistry.h>
#include <QDateTime>

#include <QImage>
#include <QPainter>
#include <QString>
#include <memory>
#include <cstdio>
#include <cstdlib>

namespace qtWasabi {
namespace {

// Feeds draw_pe's PlayList_* from the live Host playlist model.
class HostSource : public PleditDataSource {
public:
    Host *host = nullptr;
    int rowCount() override { return host ? host->playlistRowCount() : 0; }
    void rowText(int row, wchar_t *out, int cap) override {
        if (!out || cap <= 0) return;
        const QString s = host ? host->playlistRowText(row) : QString();
        int n = s.size();
        if (n > cap - 1) n = cap - 1;
        for (int i = 0; i < n; ++i) out[i] = (wchar_t)s.at(i).unicode();
        out[n] = 0;
    }
    int rowDurationSec(int row) override {
        return host ? (int)(host->playlistRowDurationMs(row) / 1000) : 0;
    }
    int currentRow() override { return host ? host->playlistCurrentRow() : -1; }
};

class PleditHostRenderer : public HolderRenderer {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QRect &r) override {
        if (r.width() <= 0 || r.height() <= 0) return;
        if (std::getenv("WASABIQT_NO_PLEDIT")) return;   // diagnostic
        // `r` is parent-local with the painter pre-translated; mouse events
        // arrive in CANVAS coords.  Publish all hit-test geometry mapped
        // through the painter transform (else clicks land rows/zones off
        // by the titlebar+menubar height).
        const QPoint canvasOff = p->transform().map(QPoint(0, 0));
        m_lastTop = canvasOff.y() + r.y();   // list-buffer top, canvas space

        // draw_pe is an EXPENSIVE software raster (a full row loop, with a
        // transient QPainter per GDI primitive).  The host repaints the whole
        // skin on a 50ms tick, so rasterising here every frame saturates the
        // GUI thread — which starves the very alpha-cache that hover/click
        // hit-testing reads, making clicks feel dead and killing button hover.
        // Cache the result and re-raster ONLY when a pixel-affecting input
        // changes (size / row count / current row / scroll).
        Sig sig;
        sig.w = r.width();
        sig.h = r.height();
        sig.rows = ctx.host ? ctx.host->playlistRowCount() : 0;
        sig.cur  = ctx.host ? ctx.host->playlistCurrentRow() : -1;
        sig.scroll = m_scroll;

        if (m_cache.isNull() || sig != m_lastSig) {
            m_src.host = ctx.host;
            pleditSetSource(&m_src);
            // Drive the row palette from the ACTIVE SKIN (Bento = grey),
            // not draw_pe's hardcoded classic green.  `wasabi.list.text`
            // resolves to color.display (147,175,185) for Bento; a classic
            // scheme that actually defines green still renders green.
            if (ctx.colors) {
                auto rgb = [&](const char *id, int dr, int dg, int db) -> unsigned {
                    const QColor c = ctx.colors->resolve(
                        QString::fromLatin1(id), ctx.gammasets, QColor(dr, dg, db));
                    return (unsigned(c.red()) << 16) | (unsigned(c.green()) << 8)
                         | unsigned(c.blue());
                };
                // Canonical Wasabi list colour roles (the Skin_PLColors set):
                // these resolve to the active skin's themed values (Winamp
                // Modern's deep blue, Bento's grey, …).  Classic-Winamp
                // defaults as fallback so a skin that defines none still
                // renders the classic green/black/white/blue.
                const unsigned norm = rgb("wasabi.list.text",
                                          0,   255, 0);   // normal row text
                const unsigned bg   = rgb("wasabi.list.background",
                                          0,   0,   0);   // list background
                const unsigned cur  = rgb("wasabi.list.text.current",
                                          255, 255, 255); // playing row text
                const unsigned selb = rgb("wasabi.list.text.selected.background",
                                          0,   0,   198); // selection bar (blue)
                if (std::getenv("WASABIQT_TRACE_PLCOLOR"))
                    std::fprintf(stderr, "[plcolor] norm=%06X bg=%06X cur=%06X selb=%06X\n",
                                 norm, bg, cur, selb);
                pleditSetColors(norm, bg, cur, selb);
            }
            pleditSetFontHeight(12);
            // ~15px rows like the reference (the glyph stays ~9px at
            // lfHeight 12; the extra leading comes from the taller pitch).
            pleditSetRowHeight(15);
            // The holder rect is the LIST AREA only — the Wasabi frame
            // paints the titlebar/menubar/button bar around it.  Tell
            // draw_pe to start row 0 near the top (small 2px pad), use the
            // whole buffer for rows, and fill the list background so the
            // area reads as one solid list (Winamp Modern has no backing
            // bevel widget behind the holder, unlike Bento).
            pleditSetListGeometry(/*top=*/2, /*reserve=*/4, /*fillBg=*/1);
            QImage img(r.width(), r.height(),
                       QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            pleditRenderToArgb(img.bits(), img.width(), img.height(),
                               (int)img.bytesPerLine(), m_scroll);
            m_cache = std::move(img);
            m_lastSig = sig;
            if (std::getenv("WASABIQT_TRACE_PLEDIT"))
                std::fprintf(stderr, "[pledit] re-raster %dx%d rows=%d cur=%d\n",
                             sig.w, sig.h, sig.rows, sig.cur);
        }
        p->drawImage(r.topLeft(), m_cache);

        // Vertical scrollbar on the right edge, rendered from the SKIN's own
        // bitmaps: up/down stepper buttons (`.left`/`.right`), a tiled track
        // (`.background`) between them, a proportional 3-slice thumb
        // (`.button`) and its grip overlay (`.grip`).  Skins without the
        // sprite set fall back to a themed flat bar.
        {
            m_sbW = 0;              // reset; set below only if a bar is drawn
            const int rows  = ctx.host ? ctx.host->playlistRowCount() : 0;
            const int rowH  = 15;   // matches pleditSetRowHeight(15)
            // The scrollbar spans the FULL list area, edge to edge — like
            // draw_pe's own slider (and the reference, whose channel even
            // overhangs the list rows by 1px each side).  The earlier
            // `+2 / -4` insets (mirroring pe_list_top / pe_list_reserve)
            // left a gap above and below the rail vs the reference.
            const int top   = r.y();
            const int bot   = r.y() + r.height();
            const int trackH = bot - top;
            if (trackH > 16) {
                const int visRows = qMax(1, trackH / rowH);
                const double frac = rows > visRows
                    ? double(visRows) / double(rows) : 1.0;
                const int maxScroll = qMax(0, rows - visRows);
                const double sf = maxScroll > 0
                    ? double(m_scroll) / double(maxScroll) : 0.0;

                QImage thumbBmp;
                if (ctx.bmp)
                    thumbBmp = ctx.bmp->imageFor(
                        QStringLiteral("wasabi.scrollbar.vertical.button"));

                if (!thumbBmp.isNull()) {
                    const int sbW = thumbBmp.width();          // 13 px
                    const int x   = r.right() - sbW + 1;
                    const QImage up = ctx.bmp->imageFor(
                        QStringLiteral("wasabi.scrollbar.vertical.left"));
                    const QImage down = ctx.bmp->imageFor(
                        QStringLiteral("wasabi.scrollbar.vertical.right"));
                    const QImage bg = ctx.bmp->imageFor(
                        QStringLiteral("wasabi.scrollbar.vertical.background"));
                    const QImage grip = ctx.bmp->imageFor(
                        QStringLiteral("wasabi.scrollbar.vertical.grip"));
                    p->save();
                    p->setRenderHint(QPainter::SmoothPixmapTransform, false);
                    const int aUp = up.isNull() ? 0 : up.height();
                    const int aDn = down.isNull() ? 0 : down.height();
                    // Tiled track behind everything, then the stepper buttons.
                    if (!bg.isNull())
                        for (int yy = top; yy < bot; yy += bg.height()) {
                            const int hh = qMin(bg.height(), bot - yy);
                            p->drawImage(QRect(x, yy, sbW, hh), bg,
                                         QRect(0, 0, bg.width(), hh));
                        }
                    if (!up.isNull())   p->drawImage(QRect(x, top, sbW, aUp), up);
                    if (!down.isNull()) p->drawImage(QRect(x, bot - aDn, sbW, aDn),
                                                     down);
                    // Proportional 3-slice thumb between the steppers, slid by
                    // the scroll fraction; the grip sprite centred on it.
                    const int trkT = top + aUp, trkB = bot - aDn;
                    const int trkH = qMax(0, trkB - trkT);
                    const int cap  = qMin(10, thumbBmp.height() / 2);
                    int thH = (rows > visRows)
                        ? qMax(thumbBmp.height(), trkH * visRows / rows) : trkH;
                    thH = qMin(thH, trkH);
                    const int ty = trkT + int(qMax(0, trkH - thH) * sf);
                    if (thH >= 2 * cap + 1) {
                        p->drawImage(QRect(x, ty, sbW, cap),
                                     thumbBmp, QRect(0, 0, sbW, cap));
                        p->drawImage(QRect(x, ty + cap, sbW, thH - 2 * cap),
                                     thumbBmp, QRect(0, cap, sbW,
                                                     thumbBmp.height() - 2 * cap));
                        p->drawImage(QRect(x, ty + thH - cap, sbW, cap), thumbBmp,
                                     QRect(0, thumbBmp.height() - cap, sbW, cap));
                    } else if (thH > 0) {
                        p->drawImage(QRect(x, ty, sbW, thH), thumbBmp);
                    }
                    if (!grip.isNull() && thH >= grip.height())
                        p->drawImage(QPoint(x + (sbW - grip.width()) / 2,
                                            ty + (thH - grip.height()) / 2), grip);
                    p->restore();
                    // Publish geometry for the mouse handlers — in CANVAS
                    // space (mouse events arrive there, drawing is local).
                    m_sbX = canvasOff.x() + x; m_sbW = sbW;
                    m_sbUpY = canvasOff.y() + top; m_sbUpH = aUp;
                    m_sbDnY = canvasOff.y() + bot - aDn; m_sbDnH = aDn;
                    m_trkT = canvasOff.y() + trkT;
                    m_trkB = canvasOff.y() + trkB;
                    m_thumbY = canvasOff.y() + ty; m_thumbH = thH;
                    m_sbVisRows = visRows; m_sbRows = rows;
                } else {
                    // Fallback: themed flat track + thumb (no skin slices).
                    const int sbW = 9;
                    const QRect sb(r.right() - sbW, top, sbW, trackH);
                    QColor sbTrack(26, 29, 32), sbTrackEdge(16, 18, 20),
                           sbThumb(84, 90, 95), sbThumbHi(120, 127, 132),
                           sbThumbLo(40, 44, 48);
                    if (ctx.colors) {
                        const QColor fg = ctx.colors->resolve(
                            QStringLiteral("wasabi.scrollbar.foreground"),
                            ctx.gammasets,
                            ctx.colors->resolve(
                                QStringLiteral("wasabi.list.text"),
                                ctx.gammasets, QColor(0, 255, 0)));
                        const QColor lbg = ctx.colors->resolve(
                            QStringLiteral("wasabi.list.background"),
                            ctx.gammasets, QColor(0, 0, 0));
                        sbTrack = lbg.lighter(160); sbTrackEdge = lbg;
                        sbThumb = fg; sbThumbHi = fg.lighter(140);
                        sbThumbLo = fg.darker(160);
                    }
                    p->save();
                    p->setRenderHint(QPainter::Antialiasing, false);
                    p->fillRect(sb, sbTrack);
                    p->fillRect(QRect(sb.x(), sb.y(), 1, sb.height()), sbTrackEdge);
                    const int thumbH = qMax(20, int(trackH * frac));
                    const int ty = sb.y() + int((trackH - thumbH) * sf);
                    const QRect thumb(sb.x() + 1, ty, sb.width() - 1, thumbH);
                    p->fillRect(thumb, sbThumb);
                    p->fillRect(QRect(thumb.x(), thumb.y(), thumb.width(), 1), sbThumbHi);
                    p->fillRect(QRect(thumb.x(), thumb.bottom(), thumb.width(), 1), sbThumbLo);
                    p->restore();
                    // Publish scroll state + geometry for the mouse/wheel
                    // handlers — WITHOUT this the flat-bar fallback left
                    // m_sbRows=0 so maxScroll()==0 and nothing could scroll
                    // (the whole reason skins without scrollbar bitmaps,
                    // e.g. HeadAMP, couldn't wheel- or drag-scroll their
                    // playlist).  No steppers in the flat bar, so the track
                    // spans the full height.
                    m_sbX = canvasOff.x() + sb.x(); m_sbW = sbW;
                    m_sbUpY = canvasOff.y() + top; m_sbUpH = 0;
                    m_sbDnY = canvasOff.y() + bot; m_sbDnH = 0;
                    m_trkT = canvasOff.y() + top;
                    m_trkB = canvasOff.y() + bot;
                    m_thumbY = canvasOff.y() + ty; m_thumbH = thumbH;
                    m_sbVisRows = visRows; m_sbRows = rows;
                }
            }
        }
    }
    bool isInteractive() const override { return true; }

    void onLeftButtonDown(QPoint pos, PaintCtx &ctx) override {
        if (!ctx.host) return;
        // Scrollbar hit-test takes priority over row selection.
        if (m_sbW > 0 && pos.x() >= m_sbX && pos.x() < m_sbX + m_sbW) {
            if (pos.y() >= m_sbUpY && pos.y() < m_sbUpY + m_sbUpH)
                setScroll(m_scroll - 1);                       // up stepper
            else if (pos.y() >= m_sbDnY && pos.y() < m_sbDnY + m_sbDnH)
                setScroll(m_scroll + 1);                       // down stepper
            else if (pos.y() >= m_thumbY && pos.y() < m_thumbY + m_thumbH) {
                m_dragging = true;                             // grab the thumb
                m_dragGrab = pos.y() - m_thumbY;
            } else if (pos.y() >= m_trkT && pos.y() < m_trkB)  // page up/down
                setScroll(m_scroll + (pos.y() < m_thumbY ? -m_sbVisRows
                                                         :  m_sbVisRows));
            return;
        }
        // Row hit-test.  draw_pe lays rows from the buffer top + pe_list_top
        // (2px), pe_rowheight (15) each; the buffer is blitted at r.y().
        const int row = m_scroll + (pos.y() - m_lastTop - 2) / 15;
        if (row < 0 || row >= ctx.host->playlistRowCount()) return;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_lastClickRow == row && now - m_lastClickMs < 350) {
            ctx.host->playlistPlayRow(row);            // double-click plays
            m_lastClickMs = 0; m_lastClickRow = -1;
        } else {
            ctx.host->playlistSetCurrentRow(row);      // single-click selects
            m_lastClickMs = now; m_lastClickRow = row;
        }
    }

    void onMouseMove(QPoint pos, PaintCtx &) override {
        if (!m_dragging || m_sbW <= 0) return;
        const int travel = qMax(1, (m_trkB - m_trkT) - m_thumbH);
        const int rel = qBound(0, pos.y() - m_dragGrab - m_trkT, travel);
        setScroll(int(double(rel) / double(travel) * maxScroll() + 0.5));
    }

    void onLeftButtonUp(QPoint, PaintCtx &) override { m_dragging = false; }

    void onMouseWheel(QPoint, int steps, PaintCtx &) override {
        setScroll(m_scroll - steps);   // wheel up → toward the top
    }

private:
    // Cache signature — any field changing forces a re-raster.
    struct Sig {
        int w = -1, h = -1, rows = -1, cur = -2, scroll = -1;
        bool operator!=(const Sig &o) const {
            return w != o.w || h != o.h || rows != o.rows
                || cur != o.cur || scroll != o.scroll;
        }
    };
    HostSource m_src;
    QImage m_cache;
    Sig m_lastSig;
    int m_scroll = 0;
    int m_lastTop = 0;
    qint64 m_lastClickMs = 0;
    int m_lastClickRow = -1;

    // Scrollbar geometry captured at paint time (canvas coords) so the
    // mouse handlers can hit-test the steppers / track / thumb.  m_sbW==0
    // means no scrollbar is currently drawn (list fits).
    int m_sbX = 0, m_sbW = 0;
    int m_sbUpY = 0, m_sbUpH = 0;     // up stepper button
    int m_sbDnY = 0, m_sbDnH = 0;     // down stepper button
    int m_trkT = 0, m_trkB = 0;       // track span (between steppers)
    int m_thumbY = 0, m_thumbH = 0;   // current thumb rect
    int m_sbVisRows = 1, m_sbRows = 0;
    bool m_dragging = false;
    int m_dragGrab = 0;               // cursor offset within the thumb

    int maxScroll() const { return qMax(0, m_sbRows - m_sbVisRows); }
    void setScroll(int s) { m_scroll = qBound(0, s, maxScroll()); }
};

}  // anonymous

void installPleditHostFactory() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    registerHolderRenderer(
        QStringLiteral("{45F3F7C1-A6F3-4ee6-A15E-125E92FC3F8D}"),
        [](const QRect &) -> std::unique_ptr<HolderRenderer> {
            return std::make_unique<PleditHostRenderer>();
        });
}

}  // namespace qtWasabi
