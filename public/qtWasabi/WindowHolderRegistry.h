#pragma once
//
// WindowHolderRegistry — pluggable dispatch for the contents of a
// `<wasabi:windowholder hold="guid:{…}"/>` slot.
//
// Before this header, `WindowHolder::paint()` inlined a hardcoded
// switch over the canonical Wasabi GUIDs (AVS, video, playlist,
// library).  That worked while qtWasabi only had its own internal
// substitutes to install in those slots; once we want to host
// ported `gen_ml`-style plugins that ALSO want to claim a GUID,
// the inline switch needs to give way to a registry the plugin
// can wire itself into at static-init.
//
// The registry holds one `HolderFactory` per GUID (canonical
// lowercase form, no `guid:` prefix).  When a windowholder paints,
// `WindowHolder` consults the registry first; if a factory is
// registered it instantiates a per-holder `HolderRenderer` and
// delegates the visible chrome to it.  The legacy in-widget
// rendering (AVS/Video fallback for slots nobody claimed) stays
// as the bottom of the chain.
//
// Skin-agnostic: a host modern skin's XML stays unchanged, only
// the back-end provider for a given GUID is swappable.
//

#include <QHash>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

#include <functional>
#include <memory>

class QPainter;

namespace qtWasabi {

struct PaintCtx;

// HolderRenderer — per-instance backing for one windowholder.
// Subclassed by:
//   * gen_ml-host MlHostWidget — paints the navigation tree +
//     content shell.
//   * The legacy MediaLibraryPanel substitute (kept as a fallback).
//   * ml.dll-equivalent plugin hosts.
//
// The renderer paints into the windowholder's allocated rect,
// receives mouse / keyboard events routed by the holder, and
// reports whether it claims interactivity (so the engine's hit-
// test treats it as a real receiver rather than skipping).
class HolderRenderer {
public:
    virtual ~HolderRenderer() = default;

    // Paint into the host's QPainter.  `rect` is the windowholder's
    // resolved rect in skin-canvas coords; the painter has NOT yet
    // been translated to it — implementations can paint absolutely
    // or save/translate themselves.  `ctx` carries the bitmap +
    // color + host pointers the rest of the engine paints with.
    virtual void paint(QPainter *p, PaintCtx &ctx, const QRect &rect) = 0;

    // Hit-test feedback for the host's `WindowHolder::
    // isInteractive`.  Default: yes.
    virtual bool isInteractive() const { return true; }

    // Mouse event routing.  Coordinates are in skin-canvas space.
    virtual void onLeftButtonDown(QPoint /*pos*/, PaintCtx & /*ctx*/) {}
    virtual void onLeftButtonUp  (QPoint /*pos*/, PaintCtx & /*ctx*/) {}
    virtual void onMouseMove     (QPoint /*pos*/, PaintCtx & /*ctx*/) {}
    virtual void onMouseWheel    (QPoint /*pos*/, int /*steps*/, PaintCtx & /*ctx*/) {}
};

// Factory signature — invoked once per windowholder instance the
// first time it paints.  Receives the holder's resolved rect so
// renderers that prefer fixed sizing can capture it on first paint.
using HolderFactory =
    std::function<std::unique_ptr<HolderRenderer>(const QRect &initialRect)>;

// ── Registry API ────────────────────────────────────────────────
//
// Each registration is keyed by the canonical lowercased GUID
// string (matching the form the windowholder's `hold=` attribute
// uses after `eqi`-normalisation).  Convenience aliases (`guid:avs`
// → `guid:{0000000A-…}`) should be registered under their canonical
// GUID — the holder normalises before lookup.

// Register a factory for the given GUID.  Replaces any existing
// factory.  GUID may be supplied with or without the `guid:` prefix;
// brackets and case are normalised.
void registerHolderRenderer(const QString &guid, HolderFactory factory);

// Milliseconds-epoch of the LAST paint of any windowholder hosting the
// given GUID (0 = never).  A recently-painted holder means the component
// is DOCKED-visible — Wasabi's isNamedWindowVisible counts that (the
// drawer detach flow gates on it).  Accepts guid:/alias spellings.
qint64 holderLastPaintedMs(const QString &holdRef);

// Unregister.  Safe to call with an unknown GUID.
void unregisterHolderRenderer(const QString &guid);

// ── Holder frame provider ───────────────────────────────────────
//
// A windowholder slot (AVS, video) is normally backed in real Winamp
// by an external surface (the AVS plugin's GL window, a DirectShow
// video renderer) that the engine can't host.  When the embedder CAN
// produce pixels for such a slot — e.g. an offscreen MilkDrop frame
// read back to a QImage for a DETACHED window that has no GL of its
// own — it registers a frame provider here.  `WindowHolder::paint`
// queries it per-paint by the slot's bare-GUID key and target size;
// a non-null QImage is blitted over the slot, replacing the black
// fill / album-art fallback.  Null = no frame, fall through to the
// default.  One provider total (the embedder fans out by GUID key).
using HolderFrameProvider =
    std::function<QImage(const QString &guidKey, const QSize &size)>;

void registerHolderFrameProvider(HolderFrameProvider provider);

// Query the registered provider; returns a null QImage if none is
// registered or it produced no frame.  `guidKey` is the bare lowercase
// "{...}" form (see WindowHolder's guidKey()).
QImage holderFrameFor(const QString &guidKey, const QSize &size);

// Lookup; returns null if no factory registered.  Used internally
// by `WindowHolderWidget::paint`.
const HolderFactory *lookupHolderRenderer(const QString &guid);

}  // namespace qtWasabi
