// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// wingdi-stubs.cpp — link-time bodies for the GDI surface that gen_ml
// + ml_* code calls.  The HDC → QPainter mapping lets the code paths
// that go through GDI calls (background fills inside CDDS_PREPAINT,
// simple status-bar text) composite onto our render buffer.
//
// HDC and HBITMAP have real handle-registry backings because the
// calling code chains (HDC →) SelectObject → GetObject → BitBlt
// and a null HDC anywhere in that chain breaks the rest.  The
// other GDI objects (HBRUSH, HPEN, HFONT) are backed by real handles
// so callers' null checks (`if (!hbr) error`) pass and the brush
// parameters are applied once a real painter is available.
//

#include "win32/handle-registry.h"
#include "win32/wingdi.h"

#include <QPainter>
#include <QImage>
#include <QColor>
#include <QPoint>
#include <QRect>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace qtWasabi::wasabi_compat {

// HDC backing — holds a QPainter pointer.  When BeginPaint is
// called against a HWND that has an active paint pass, we hand
// back this DC bound to the host's QPainter.  A null painter lets
// calls gracefully no-op until a real one is bound.
class DcObject {
public:
    QPainter *painter = nullptr;
    COLORREF  textColor = 0;
    COLORREF  bkColor   = 0xFFFFFF;
    int       bkMode    = OPAQUE;
    int       rop2      = R2_COPYPEN;
    // The bitmap currently selected into this (memory) DC.
    // wa_dlg reads its theme colors back via GetPixel against a
    // CreateCompatibleDC + SelectObject(genex) chain, so the DC must
    // remember which bitmap is bound.
    HBITMAP   boundBitmap = nullptr;
    // Raster core: brush/pen/font bound via SelectObject.  These
    // hold GdiObj* handles (see below); FillRect/Rectangle/TextOut read
    // their color/width/face out of the binding.
    HGDIOBJ   boundBrush  = nullptr;
    HGDIOBJ   boundPen    = nullptr;
    HGDIOBJ   boundFont   = nullptr;
    QPoint    penPos{0, 0};   // current position for MoveToEx/LineTo
    // SaveDC/RestoreDC stack — gen_ml owner-draw brackets paint with
    // them so per-row colour/pen changes don't leak.  Snapshots the
    // mutable DC state (not the bound bitmap target's pixels).
    struct State {
        COLORREF textColor, bkColor; int bkMode, rop2;
        HBITMAP boundBitmap; HGDIOBJ boundBrush, boundPen, boundFont;
        QPoint penPos;
    };
    std::vector<State> stateStack;
};

// HBITMAP backing — wraps a QImage.
class ConcreteBitmap : public BitmapObject {
public:
    QImage image;
};

// Per-DC registry, keyed on the same UINT_PTR encoding the rest
// of wasabi-compat uses.  Lives in its own type so DC handles
// can't be confused with HWND/HBITMAP.
class DcRegistryEntry {
public:
    std::unique_ptr<DcObject> dc;
};

namespace {

// Lazy-init singleton holding all live DC backings.  Keyed by
// integer slot id, same generation+id encoding as the main
// registry — keeps the lookup safe across destroy-recycle.
struct DcRegistry {
    std::mutex                                            mu;
    std::unordered_map<uint32_t, std::unique_ptr<DcObject>> entries;
    std::atomic<uint32_t>                                 next_id{1};
    std::atomic<uint32_t>                                 gen{1};

    HDC create() {
        auto obj = std::make_unique<DcObject>();
        const uint32_t id = next_id.fetch_add(1);
        const uint32_t g  = gen.fetch_add(1);
        const UINT_PTR raw = (UINT_PTR(g) << 32) | UINT_PTR(id);
        std::lock_guard<std::mutex> lk(mu);
        entries.emplace(id, std::move(obj));
        return reinterpret_cast<HDC>(static_cast<uintptr_t>(raw));
    }

    DcObject *lookup(HDC h) {
        if (!h) return nullptr;
        const UINT_PTR raw = static_cast<UINT_PTR>(reinterpret_cast<uintptr_t>(h));
        const uint32_t id  = static_cast<uint32_t>(raw & 0xFFFFFFFFu);
        std::lock_guard<std::mutex> lk(mu);
        auto it = entries.find(id);
        return it == entries.end() ? nullptr : it->second.get();
    }

    void destroy(HDC h) {
        if (!h) return;
        const UINT_PTR raw = static_cast<UINT_PTR>(reinterpret_cast<uintptr_t>(h));
        const uint32_t id  = static_cast<uint32_t>(raw & 0xFFFFFFFFu);
        std::lock_guard<std::mutex> lk(mu);
        entries.erase(id);
    }
};

DcRegistry &dcRegistry() {
    static DcRegistry r;
    return r;
}

// HBRUSH/HPEN/HFONT sentinel.  Used where a real backing object is
// not needed; we hand back a fixed non-null pointer so callers'
// null-checks pass.  reinterpret_cast isn't a constexpr context — keep
// the sentinel as a runtime-const inline function instead.
inline void *kStubGdiObj() {
    return reinterpret_cast<void *>(uintptr_t(1));
}

// ── Real HBRUSH / HPEN / HFONT backing (raster core) ──────────
//
// Brushes/pens/fonts are leaf value-objects: a color (+ pen width /
// font params).  We hand back the heap GdiObj* AS the handle and track
// liveness in a registry so SelectObject can tell a brush from a
// bitmap (gen-tagged handle) without numeric-collision risk — the
// registry membership test is exact.  Stock objects live in the same
// registry, flagged so DeleteObject leaves them alone.
enum class GdiKind { Brush, Pen, Font };
struct GdiObj {
    GdiKind kind = GdiKind::Brush;
    COLORREF color = 0;        // brush color / pen color / text-ignored
    int  penWidth = 1;
    int  penStyle = 0;         // PS_SOLID
    bool hollow   = false;     // NULL_BRUSH / NULL_PEN — draw nothing
    bool stock    = false;     // GetStockObject singleton — never freed
    // Font parameters, set by CreateFontW.
    int  fontHeight = 0;
    int  fontWeight = 0;
    std::wstring faceName;
};

struct GdiObjRegistry {
    std::mutex mu;
    std::unordered_map<void *, std::unique_ptr<GdiObj>> objs;
    void *add(std::unique_ptr<GdiObj> o) {
        void *k = o.get();
        std::lock_guard<std::mutex> lk(mu);
        objs.emplace(k, std::move(o));
        return k;
    }
    GdiObj *lookup(void *h) {
        if (!h) return nullptr;
        std::lock_guard<std::mutex> lk(mu);
        auto it = objs.find(h);
        return it == objs.end() ? nullptr : it->second.get();
    }
    void remove(void *h) {
        std::lock_guard<std::mutex> lk(mu);
        objs.erase(h);
    }
};
GdiObjRegistry &gdiObjs() { static GdiObjRegistry r; return r; }

// Lazily-built GetStockObject singletons, one per index, cached so
// repeated calls return the same handle (GDI contract).
HGDIOBJ getStock(int idx) {
    static std::mutex m;
    static std::unordered_map<int, void *> cache;
    std::lock_guard<std::mutex> lk(m);
    auto it = cache.find(idx);
    if (it != cache.end()) return reinterpret_cast<HGDIOBJ>(it->second);
    auto o = std::make_unique<GdiObj>();
    o->stock = true;
    switch (idx) {
        case WHITE_BRUSH:  o->kind = GdiKind::Brush; o->color = RGB(255,255,255); break;
        case LTGRAY_BRUSH: o->kind = GdiKind::Brush; o->color = RGB(192,192,192); break;
        case GRAY_BRUSH:   o->kind = GdiKind::Brush; o->color = RGB(128,128,128); break;
        case DKGRAY_BRUSH: o->kind = GdiKind::Brush; o->color = RGB(64,64,64);    break;
        case BLACK_BRUSH:  o->kind = GdiKind::Brush; o->color = RGB(0,0,0);       break;
        case NULL_BRUSH:   o->kind = GdiKind::Brush; o->hollow = true;            break;
        case WHITE_PEN:    o->kind = GdiKind::Pen;   o->color = RGB(255,255,255); break;
        case BLACK_PEN:    o->kind = GdiKind::Pen;   o->color = RGB(0,0,0);       break;
        case NULL_PEN:     o->kind = GdiKind::Pen;   o->hollow = true;            break;
        default:           o->kind = GdiKind::Font;  o->fontHeight = 11;          break;
    }
    void *h = gdiObjs().add(std::move(o));
    cache.emplace(idx, h);
    return reinterpret_cast<HGDIOBJ>(h);
}

// COLORREF (0x00BBGGRR) → QColor.  Alpha forced opaque.
inline QColor colorrefToQColor(COLORREF c) {
    return QColor(GetRValue(c), GetGValue(c), GetBValue(c));
}

// Resolve the QImage a (memory) DC draws into via its bound bitmap.
inline ConcreteBitmap *boundImage(DcObject *dc) {
    if (!dc || !dc->boundBitmap) return nullptr;
    auto *bo = lookupHandle<BitmapObject>(dc->boundBitmap);
    return bo ? static_cast<ConcreteBitmap *>(bo) : nullptr;
}

// Best-effort ROP → Qt composition.  SRCCOPY (the dominant gen_ml path
// on 32bpp skins) is an exact replace; the legacy 1-bit-mask ROPs map
// to the closest blend.  Skin bitmaps load alpha=0xFF so Source is the
// faithful copy.
inline QPainter::CompositionMode ropToMode(DWORD rop) {
    switch (rop) {
        case SRCCOPY:   return QPainter::CompositionMode_Source;
        case SRCAND:    return QPainter::CompositionMode_Multiply;
        case SRCPAINT:  return QPainter::CompositionMode_Plus;
        case SRCINVERT: return QPainter::CompositionMode_Xor;
        default:        return QPainter::CompositionMode_SourceOver;
    }
}

// Build a QFont from the DC's bound HFONT.  gen_ml + wa_dlg draw their
// text with real GDI fonts (CreateFontW → Tahoma/Arial), NOT the classic
// bitmap font (that's the main-window playlist path), so QPainter::drawText
// with the selected face/size is the faithful route.
inline QFont fontFromDc(DcObject *dc) {
    QFont f;
    GdiObj *fo = (dc && dc->boundFont) ? gdiObjs().lookup(dc->boundFont) : nullptr;
    if (fo && fo->kind == GdiKind::Font) {
        if (!fo->faceName.empty())
            f.setFamily(QString::fromWCharArray(fo->faceName.c_str()));
        if (fo->fontHeight > 0) f.setPixelSize(fo->fontHeight);
        if (fo->fontWeight >= 700) f.setBold(true);
    } else {
        f.setPixelSize(11);
    }
    return f;
}

}  // anonymous

}  // namespace qtWasabi::wasabi_compat

extern "C" {

// ── DC lifecycle ───────────────────────────────────────────────

HDC WINAPI GetDC(HWND /*hwnd*/)                      { return qtWasabi::wasabi_compat::dcRegistry().create(); }
HDC WINAPI GetWindowDC(HWND /*hwnd*/)                { return qtWasabi::wasabi_compat::dcRegistry().create(); }
int WINAPI ReleaseDC(HWND /*hwnd*/, HDC hdc)         { qtWasabi::wasabi_compat::dcRegistry().destroy(hdc); return 1; }
HDC WINAPI CreateCompatibleDC(HDC /*src*/)           { return qtWasabi::wasabi_compat::dcRegistry().create(); }
BOOL WINAPI DeleteDC(HDC hdc)                        { qtWasabi::wasabi_compat::dcRegistry().destroy(hdc); return TRUE; }

int WINAPI SaveDC(HDC hdc) {
    using namespace qtWasabi::wasabi_compat;
    auto *dc = dcRegistry().lookup(hdc);
    if (!dc) return 0;
    dc->stateStack.push_back({dc->textColor, dc->bkColor, dc->bkMode,
                              dc->rop2, dc->boundBitmap, dc->boundBrush,
                              dc->boundPen, dc->boundFont, dc->penPos});
    return static_cast<int>(dc->stateStack.size());
}
BOOL WINAPI RestoreDC(HDC hdc, int nSaved) {
    using namespace qtWasabi::wasabi_compat;
    auto *dc = dcRegistry().lookup(hdc);
    if (!dc || dc->stateStack.empty()) return FALSE;
    size_t target;
    if (nSaved < 0) {                       // relative: -1 = most recent
        const size_t back = static_cast<size_t>(-nSaved);
        if (back > dc->stateStack.size()) return FALSE;
        target = dc->stateStack.size() - back;
    } else {                                // absolute 1-based level
        if (nSaved < 1 || static_cast<size_t>(nSaved) > dc->stateStack.size())
            return FALSE;
        target = static_cast<size_t>(nSaved) - 1;
    }
    const auto st = dc->stateStack[target];
    dc->textColor = st.textColor; dc->bkColor = st.bkColor;
    dc->bkMode = st.bkMode;       dc->rop2 = st.rop2;
    dc->boundBitmap = st.boundBitmap; dc->boundBrush = st.boundBrush;
    dc->boundPen = st.boundPen;       dc->boundFont = st.boundFont;
    dc->penPos = st.penPos;
    dc->stateStack.resize(target);  // discard the restored level + any above
    return TRUE;
}

HBITMAP WINAPI CreateCompatibleBitmap(HDC /*src*/, int w, int h) {
    using namespace qtWasabi::wasabi_compat;
    auto b = std::make_unique<ConcreteBitmap>();
    b->image = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    b->image.fill(Qt::transparent);
    return registerHandle<BitmapObject>(std::move(b));
}

HBITMAP WINAPI CreateDIBSection(HDC /*src*/, const BITMAPINFO *bi, UINT /*usage*/,
                                  void **ppvBits, HANDLE /*sect*/, DWORD /*offs*/) {
    using namespace qtWasabi::wasabi_compat;
    int w = bi ? bi->bmiHeader.biWidth  : 0;
    int h = bi ? std::abs(bi->bmiHeader.biHeight) : 0;
    auto b = std::make_unique<ConcreteBitmap>();
    b->image = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    b->image.fill(Qt::transparent);
    if (ppvBits) *ppvBits = b->image.bits();
    return registerHandle<BitmapObject>(std::move(b));
}

// ── Object creation ────────────────────────────────────────────

HBRUSH  WINAPI CreateSolidBrush(COLORREF c) {
    using namespace qtWasabi::wasabi_compat;
    auto o = std::make_unique<GdiObj>();
    o->kind = GdiKind::Brush;
    o->color = c;
    return reinterpret_cast<HBRUSH>(gdiObjs().add(std::move(o)));
}
HPEN    WINAPI CreatePen(int style, int width, COLORREF c) {
    using namespace qtWasabi::wasabi_compat;
    auto o = std::make_unique<GdiObj>();
    o->kind = GdiKind::Pen;
    o->color = c;
    o->penStyle = style;
    o->penWidth = width > 0 ? width : 1;
    o->hollow = (style == 5 /*PS_NULL*/);
    return reinterpret_cast<HPEN>(gdiObjs().add(std::move(o)));
}
HFONT   WINAPI CreateFontW(int h, int /*w*/, int, int, int weight, DWORD, DWORD, DWORD,
                              DWORD, DWORD, DWORD, DWORD, DWORD, LPCWSTR face) {
    using namespace qtWasabi::wasabi_compat;
    auto o = std::make_unique<GdiObj>();
    o->kind = GdiKind::Font;
    o->fontHeight = h < 0 ? -h : h;
    o->fontWeight = weight;
    if (face) o->faceName = face;
    return reinterpret_cast<HFONT>(gdiObjs().add(std::move(o)));
}
HGDIOBJ WINAPI GetStockObject(int idx)    { return qtWasabi::wasabi_compat::getStock(idx); }
// wa_dlg support.  We always nearest-neighbour-stretch, so the mode is
// advisory — return a sane previous value.
int      WINAPI SetStretchBltMode(HDC, int) { return COLORONCOLOR; }
COLORREF WINAPI GetNearestColor(HDC, COLORREF c) { return c; }  // no palette
HBRUSH   WINAPI CreateBrushIndirect(const LOGBRUSH *lb) {
    using namespace qtWasabi::wasabi_compat;
    auto o = std::make_unique<GdiObj>();
    o->kind = GdiKind::Brush;
    o->color = lb ? lb->lbColor : 0;
    o->hollow = lb && lb->lbStyle == 1 /*BS_NULL / HOLLOW*/;
    return reinterpret_cast<HBRUSH>(gdiObjs().add(std::move(o)));
}
// Region ops — wa_dlg only touches them in the WM_PAINT background-erase
// path gated on ps.fErase, which our BeginPaint leaves FALSE, so they
// never run at runtime; minimal bodies so DrawChildWindowBorders compiles
// + degrades to "no clipped erase" (the border LINES still draw via the
// raster core).  A QRegion-backed HRGN would be the fidelity upgrade.
HRGN WINAPI CreateRectRgn(int, int, int, int) {
    return reinterpret_cast<HRGN>(qtWasabi::wasabi_compat::kStubGdiObj());
}
HRGN WINAPI CreateRectRgnIndirect(const RECT *) {
    return reinterpret_cast<HRGN>(qtWasabi::wasabi_compat::kStubGdiObj());
}
int  WINAPI CombineRgn(HRGN, HRGN, HRGN, int) { return SIMPLEREGION; }
int  WINAPI FillRgn(HDC, HRGN, HBRUSH)        { return 1; }
HGDIOBJ WINAPI SelectObject(HDC hdc, HGDIOBJ obj) {
    using namespace qtWasabi::wasabi_compat;
    // If `obj` is a registered bitmap, bind it to the DC and return
    // the previously-bound bitmap (the GDI SelectObject contract).
    // Brush/pen/font selection binds through the raster core, kept
    // additive so the ml_* plugins (which never paint) are unaffected.
    if (auto *dc = dcRegistry().lookup(hdc)) {
        // Brush/pen/font: exact registry membership test (no numeric
        // collision with gen-tagged bitmap handles).  Check this FIRST.
        if (auto *g = gdiObjs().lookup(obj)) {
            HGDIOBJ prev = nullptr;
            switch (g->kind) {
                case GdiKind::Brush: prev = dc->boundBrush; dc->boundBrush = obj; break;
                case GdiKind::Pen:   prev = dc->boundPen;   dc->boundPen   = obj; break;
                case GdiKind::Font:  prev = dc->boundFont;  dc->boundFont  = obj; break;
            }
            return prev ? prev : (HGDIOBJ)kStubGdiObj();
        }
        if (obj && lookupHandle<BitmapObject>(reinterpret_cast<HBITMAP>(obj))) {
            HGDIOBJ prev = reinterpret_cast<HGDIOBJ>(dc->boundBitmap);
            dc->boundBitmap = reinterpret_cast<HBITMAP>(obj);
            return prev ? prev : (HGDIOBJ)kStubGdiObj();
        }
    }
    return (HGDIOBJ)kStubGdiObj();
}
BOOL    WINAPI DeleteObject(HGDIOBJ obj)  {
    using namespace qtWasabi::wasabi_compat;
    if (auto *g = gdiObjs().lookup(obj)) {
        if (!g->stock) gdiObjs().remove(obj);   // stock objects persist
        return TRUE;
    }
    return TRUE;   // bitmaps freed via the handle registry's own lifecycle
}
int     WINAPI GetObjectW(HGDIOBJ obj, int n, LPVOID buf) {
    using namespace qtWasabi::wasabi_compat;
    // Real BITMAP query for a registered bitmap handle — wa_dlg +
    // gen_ml's skinned widgets read bmWidth/bmHeight to size their
    // 9-slice blits.
    if (obj && buf && n >= (int)sizeof(BITMAP)) {
        if (auto *bo = lookupHandle<BitmapObject>(reinterpret_cast<HBITMAP>(obj))) {
            auto *bmp = static_cast<ConcreteBitmap *>(bo);
            BITMAP bm{};
            bm.bmType        = 0;
            bm.bmWidth       = bmp->image.width();
            bm.bmHeight      = bmp->image.height();
            bm.bmWidthBytes  = (LONG)bmp->image.bytesPerLine();
            bm.bmPlanes      = 1;
            bm.bmBitsPixel   = 32;
            bm.bmBits        = bmp->image.bits();
            *reinterpret_cast<BITMAP *>(buf) = bm;
            return (int)sizeof(BITMAP);
        }
    }
    if (buf && n > 0) {
        for (int i = 0; i < n; ++i) ((BYTE *)buf)[i] = 0;
    }
    return n;
}

// ── DC state ───────────────────────────────────────────────────

COLORREF WINAPI SetTextColor(HDC hdc, COLORREF c) {
    using namespace qtWasabi::wasabi_compat;
    auto *dc = dcRegistry().lookup(hdc);
    if (!dc) return 0;
    COLORREF prev = dc->textColor;
    dc->textColor = c;
    return prev;
}
COLORREF WINAPI GetTextColor(HDC hdc) {
    using namespace qtWasabi::wasabi_compat;
    auto *dc = dcRegistry().lookup(hdc);
    return dc ? dc->textColor : 0;
}
COLORREF WINAPI SetBkColor(HDC hdc, COLORREF c) {
    using namespace qtWasabi::wasabi_compat;
    auto *dc = dcRegistry().lookup(hdc);
    if (!dc) return 0;
    COLORREF prev = dc->bkColor;
    dc->bkColor = c;
    return prev;
}
COLORREF WINAPI GetBkColor(HDC hdc) {
    using namespace qtWasabi::wasabi_compat;
    auto *dc = dcRegistry().lookup(hdc);
    return dc ? dc->bkColor : 0xFFFFFF;
}
int WINAPI SetBkMode(HDC hdc, int m) {
    using namespace qtWasabi::wasabi_compat;
    auto *dc = dcRegistry().lookup(hdc);
    if (!dc) return 0;
    int prev = dc->bkMode;
    dc->bkMode = m;
    return prev;
}
int WINAPI GetBkMode(HDC hdc) {
    using namespace qtWasabi::wasabi_compat;
    auto *dc = dcRegistry().lookup(hdc);
    return dc ? dc->bkMode : OPAQUE;
}
int WINAPI SetROP2(HDC hdc, int r) {
    using namespace qtWasabi::wasabi_compat;
    auto *dc = dcRegistry().lookup(hdc);
    if (!dc) return 0;
    int prev = dc->rop2;
    dc->rop2 = r;
    return prev;
}
int WINAPI GetROP2(HDC hdc) {
    using namespace qtWasabi::wasabi_compat;
    auto *dc = dcRegistry().lookup(hdc);
    return dc ? dc->rop2 : R2_COPYPEN;
}

// ── Drawing primitives ─────────────────────────────────────────
//
// These map to real QPainter calls and return TRUE so the calling
// code doesn't error-branch.  Rendering for gen_ml's owner-draw
// arrives via NM_CUSTOMDRAW routed through the WindowObject::paint path.

// Raster-into-bound-bitmap: each primitive opens a SHORT-LIVED QPainter
// scoped to the call (a persistent QPainter on a QImage would block the
// QImage::bits()/setPixel() that CreateDIBSection/GetPixel/SetPixel use).
BOOL WINAPI BitBlt(HDC hdcDst, int x, int y, int w, int h,
                   HDC hdcSrc, int sx, int sy, DWORD rop) {
    using namespace qtWasabi::wasabi_compat;
    if (w <= 0 || h <= 0) return TRUE;
    auto *dst = dcRegistry().lookup(hdcDst);
    auto *dimg = boundImage(dst);
    if (!dimg) return TRUE;
    // PATCOPY/PATINVERT fill the dest with the bound brush (no source).
    if (rop == PATCOPY) {
        GdiObj *br = dst->boundBrush ? gdiObjs().lookup(dst->boundBrush) : nullptr;
        if (br && !br->hollow) {
            QPainter p(&dimg->image);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.fillRect(QRect(x, y, w, h), colorrefToQColor(br->color));
        }
        return TRUE;
    }
    auto *src = dcRegistry().lookup(hdcSrc);
    auto *simg = boundImage(src);
    if (!simg) return TRUE;
    QPainter p(&dimg->image);
    p.setCompositionMode(ropToMode(rop));
    p.drawImage(QRect(x, y, w, h), simg->image, QRect(sx, sy, w, h));
    return TRUE;
}
BOOL WINAPI StretchBlt(HDC hdcDst, int x, int y, int w, int h,
                       HDC hdcSrc, int sx, int sy, int sw, int sh, DWORD rop) {
    using namespace qtWasabi::wasabi_compat;
    if (w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return TRUE;
    auto *dst = dcRegistry().lookup(hdcDst);
    auto *src = dcRegistry().lookup(hdcSrc);
    auto *dimg = boundImage(dst);
    auto *simg = boundImage(src);
    if (!dimg || !simg) return TRUE;
    QPainter p(&dimg->image);
    p.setCompositionMode(ropToMode(rop));
    // Winamp 9-slice corners are nearest-neighbour — no smoothing.
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(QRect(x, y, w, h), simg->image, QRect(sx, sy, sw, sh));
    return TRUE;
}
BOOL WINAPI Rectangle(HDC hdc, int l, int t, int r, int b) {
    using namespace qtWasabi::wasabi_compat;
    auto *dc = dcRegistry().lookup(hdc);
    auto *img = boundImage(dc);
    if (!img) return TRUE;
    QPainter p(&img->image);
    GdiObj *br = dc->boundBrush ? gdiObjs().lookup(dc->boundBrush) : nullptr;
    GdiObj *pn = dc->boundPen   ? gdiObjs().lookup(dc->boundPen)   : nullptr;
    if (br && !br->hollow) p.setBrush(colorrefToQColor(br->color));
    else                   p.setBrush(Qt::NoBrush);
    if (pn && !pn->hollow) p.setPen(QPen(colorrefToQColor(pn->color), pn->penWidth));
    else if (pn)           p.setPen(Qt::NoPen);
    else                   p.setPen(QPen(Qt::black));   // GDI default BLACK_PEN
    // GDI Rectangle spans [l,t]..(r-1,b-1) inclusive border + fill.
    p.drawRect(QRect(l, t, (r - l) - 1, (b - t) - 1));
    return TRUE;
}
int WINAPI FillRect(HDC hdc, const RECT *rc, HBRUSH hbr) {
    using namespace qtWasabi::wasabi_compat;
    auto *dc = dcRegistry().lookup(hdc);
    if (!dc || !rc) return 1;
    auto *img = boundImage(dc);
    if (!img) return 1;
    GdiObj *br = gdiObjs().lookup(hbr);
    if (br && br->hollow) return 1;
    QColor col = br ? colorrefToQColor(br->color) : QColor(0, 0, 0);
    QPainter p(&img->image);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    // FillRect rect is [left,top,right,bottom) — exclusive bottom-right.
    p.fillRect(QRect(rc->left, rc->top, rc->right - rc->left,
                     rc->bottom - rc->top), col);
    return 1;
}
int WINAPI FrameRect(HDC hdc, const RECT *rc, HBRUSH hbr) {
    using namespace qtWasabi::wasabi_compat;
    auto *dc = dcRegistry().lookup(hdc);
    if (!dc || !rc) return 1;
    auto *img = boundImage(dc);
    if (!img) return 1;
    GdiObj *br = gdiObjs().lookup(hbr);
    if (br && br->hollow) return 1;
    QColor col = br ? colorrefToQColor(br->color) : QColor(0, 0, 0);
    QPainter p(&img->image);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(col, 1));
    // FrameRect draws a 1px border on the rect edges (bottom/right excl).
    p.drawRect(QRect(rc->left, rc->top, (rc->right - rc->left) - 1,
                     (rc->bottom - rc->top) - 1));
    return 1;
}
BOOL WINAPI MoveToEx(HDC hdc, int x, int y, LPPOINT prev) {
    using namespace qtWasabi::wasabi_compat;
    auto *dc = dcRegistry().lookup(hdc);
    if (!dc) return TRUE;
    if (prev) { prev->x = dc->penPos.x(); prev->y = dc->penPos.y(); }
    dc->penPos = QPoint(x, y);
    return TRUE;
}
BOOL WINAPI LineTo(HDC hdc, int x, int y) {
    using namespace qtWasabi::wasabi_compat;
    auto *dc = dcRegistry().lookup(hdc);
    auto *img = boundImage(dc);
    if (!img) { if (dc) dc->penPos = QPoint(x, y); return TRUE; }
    GdiObj *pn = dc->boundPen ? gdiObjs().lookup(dc->boundPen) : nullptr;
    if (!(pn && pn->hollow)) {
        QPainter p(&img->image);
        QColor col = pn ? colorrefToQColor(pn->color) : QColor(0, 0, 0);
        p.setPen(QPen(col, pn ? pn->penWidth : 1));
        p.drawLine(dc->penPos, QPoint(x, y));
    }
    dc->penPos = QPoint(x, y);
    return TRUE;
}
COLORREF WINAPI SetPixel(HDC hdc, int x, int y, COLORREF c) {
    using namespace qtWasabi::wasabi_compat;
    if (auto *dc = dcRegistry().lookup(hdc)) {
        if (dc->boundBitmap) {
            if (auto *bo = lookupHandle<BitmapObject>(dc->boundBitmap)) {
                auto *bmp = static_cast<ConcreteBitmap *>(bo);
                if (x >= 0 && y >= 0 && x < bmp->image.width() &&
                    y < bmp->image.height())
                    bmp->image.setPixel(x, y,
                        qRgb(GetRValue(c), GetGValue(c), GetBValue(c)));
            }
        }
    }
    return c;
}
// Read a pixel from the bitmap selected into the DC.  wa_dlg's
// WADlg_init does exactly this against the genex bitmap to recover
// the 24 theme colours.  COLORREF is 0x00BBGGRR; QRgb is 0xAARRGGBB.
COLORREF WINAPI GetPixel(HDC hdc, int x, int y) {
    using namespace qtWasabi::wasabi_compat;
    auto *dc = dcRegistry().lookup(hdc);
    if (!dc || !dc->boundBitmap) return 0xFFFFFFFF /*CLR_INVALID*/;
    auto *bo = lookupHandle<BitmapObject>(dc->boundBitmap);
    if (!bo) return 0xFFFFFFFF;
    auto *bmp = static_cast<ConcreteBitmap *>(bo);
    if (x < 0 || y < 0 || x >= bmp->image.width() || y >= bmp->image.height())
        return 0xFFFFFFFF;
    const QRgb px = bmp->image.pixel(x, y);
    return RGB(qRed(px), qGreen(px), qBlue(px));
}
// gen_ml text — QPainter::drawText with the bound font.  Guarded on a
// live QGuiApplication (the font DB needs it); calls before app init
// (e.g. static-init smokes) no-op gracefully.  GDI text y is the cell
// TOP; Qt's drawText baseline is y+ascent.
BOOL WINAPI TextOutW(HDC hdc, int x, int y, LPCWSTR str, int len) {
    using namespace qtWasabi::wasabi_compat;
    if (!QGuiApplication::instance() || !str || len <= 0) return TRUE;
    auto *dc = dcRegistry().lookup(hdc);
    auto *img = boundImage(dc);
    if (!img) return TRUE;
    const QString s = QString::fromWCharArray(str, len);
    QPainter p(&img->image);
    QFont f = fontFromDc(dc);
    p.setFont(f);
    QFontMetrics fm(f);
    if (dc->bkMode == OPAQUE)
        p.fillRect(QRect(x, y, fm.horizontalAdvance(s), fm.height()),
                   colorrefToQColor(dc->bkColor));
    p.setPen(colorrefToQColor(dc->textColor));
    p.drawText(x, y + fm.ascent(), s);
    return TRUE;
}
BOOL WINAPI ExtTextOutW(HDC hdc, int x, int y, UINT opts, const RECT *clip,
                        LPCWSTR str, UINT len, const INT * /*dx*/) {
    using namespace qtWasabi::wasabi_compat;
    if (!QGuiApplication::instance()) return TRUE;
    auto *dc = dcRegistry().lookup(hdc);
    auto *img = boundImage(dc);
    if (!img) return TRUE;
    QPainter p(&img->image);
    if ((opts & 0x0002u /*ETO_OPAQUE*/) && clip)
        p.fillRect(QRect(clip->left, clip->top, clip->right - clip->left,
                         clip->bottom - clip->top), colorrefToQColor(dc->bkColor));
    if (str && len > 0) {
        const QString s = QString::fromWCharArray(str, (int)len);
        QFont f = fontFromDc(dc);
        p.setFont(f);
        QFontMetrics fm(f);
        p.setPen(colorrefToQColor(dc->textColor));
        p.drawText(x, y + fm.ascent(), s);
    }
    return TRUE;
}
int WINAPI DrawTextW(HDC hdc, LPCWSTR str, int n, LPRECT rc, UINT fmt) {
    using namespace qtWasabi::wasabi_compat;
    if (!str || !rc) return 0;
    auto *dc = dcRegistry().lookup(hdc);
    if (!dc || !QGuiApplication::instance()) return 0;
    const QString s = (n < 0) ? QString::fromWCharArray(str)
                              : QString::fromWCharArray(str, n);
    QFont f = fontFromDc(dc);
    QFontMetrics fm(f);
    QRect r(rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top);
    int qf = 0;
    if (fmt & DT_CENTER)     qf |= Qt::AlignHCenter;
    else if (fmt & DT_RIGHT) qf |= Qt::AlignRight;
    else                     qf |= Qt::AlignLeft;
    if (fmt & DT_VCENTER)    qf |= Qt::AlignVCenter;
    else                     qf |= Qt::AlignTop;
    if (fmt & DT_SINGLELINE) qf |= Qt::TextSingleLine;
    if (fmt & DT_WORDBREAK)  qf |= Qt::TextWordWrap;
    // DT_END_ELLIPSIS: GDI truncates an over-wide single line with "…" so it
    // fits the rect (e.g. the playlist shrinks the title rect to leave room
    // for the right-aligned duration, then elides the title).  Qt's
    // drawText clips but does not elide, so without this the title bled over
    // the duration.  Elide here — general, every elided control benefits.
    QString s2 = s;
    if ((fmt & DT_END_ELLIPSIS) && (fmt & DT_SINGLELINE) && r.width() > 0 &&
        fm.horizontalAdvance(s) > r.width()) {
        s2 = fm.elidedText(s, Qt::ElideRight, r.width());
    }
    if (fmt & DT_CALCRECT) {
        QRect br = fm.boundingRect(r, qf, s2);
        rc->right  = rc->left + br.width();
        rc->bottom = rc->top  + br.height();
        return br.height();
    }
    auto *img = boundImage(dc);
    if (!img) return fm.height();
    QPainter p(&img->image);
    p.setFont(f);
    p.setPen(colorrefToQColor(dc->textColor));
    p.drawText(r, qf, s2);
    return fm.height();
}
BOOL WINAPI GetTextExtentPoint32W(HDC hdc, LPCWSTR str, int len, LPSIZE sz) {
    using namespace qtWasabi::wasabi_compat;
    if (!sz) return FALSE;
    sz->cx = 0; sz->cy = 0;
    if (!QGuiApplication::instance() || !str || len <= 0) return TRUE;
    QFont f = fontFromDc(dcRegistry().lookup(hdc));
    QFontMetrics fm(f);
    const QString s = QString::fromWCharArray(str, len);
    sz->cx = fm.horizontalAdvance(s);
    sz->cy = fm.height();
    return TRUE;
}

// ── BeginPaint / EndPaint ──────────────────────────────────────

HDC WINAPI BeginPaint(HWND /*hwnd*/, LPPAINTSTRUCT ps) {
    using namespace qtWasabi::wasabi_compat;
    HDC hdc = dcRegistry().create();
    if (ps) {
        ps->hdc        = hdc;
        ps->fErase     = FALSE;
        ps->rcPaint    = {0, 0, 0, 0};
        ps->fRestore   = FALSE;
        ps->fIncUpdate = FALSE;
        for (auto &b : ps->rgbReserved) b = 0;
    }
    return hdc;
}

BOOL WINAPI EndPaint(HWND /*hwnd*/, const PAINTSTRUCT *ps) {
    using namespace qtWasabi::wasabi_compat;
    if (ps) dcRegistry().destroy(ps->hdc);
    return TRUE;
}

}  // extern "C"
