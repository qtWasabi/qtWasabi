#pragma once
//
// Widget — abstract base for every Wasabi widget tag.  Mirrors real
// Wasabi's `GuiObject` interface: one C++ class per XML tag, with
// virtual paint / hitTest / mouse-event dispatch.  The class owns
// its children and its per-instance runtime state (drag offsets,
// animation timers, scroll positions, …).
//
// Divergences from Wasabi (all architecturally motivated, not corner-
// cut shortcuts — see plan file):
//
//   • `attrs` stays as QHash<QString, QString> because Maki
//     setXmlParam is purely string-keyed at runtime.  Per-widget
//     typed caches can shadow the attrs but aren't authoritative.
//   • Plain C++ virtuals, no SOM (Script Object Manager) indirection
//     — we don't ship plugins, every widget class is statically known.
//   • `std::unique_ptr<Widget>` for children rather than Wasabi's
//     PtrList<GuiObject>; we don't need a GC-style remove-by-position
//     pattern.
//
// Lifecycle:
//   1. SkinXml parses the XML doc.
//   2. Layout::Expander walks the tree.  For each element it calls
//      Widget::create(tag) which returns a fresh instance of the
//      appropriate subclass (Layer, Button, …) wrapped in a
//      unique_ptr.  Attrs are copied into `attrs`; children are
//      expanded recursively into `children`.
//   3. TreePainter::paintTree calls `root->paint(p, ctx, canvas)` —
//      no central widget-type switch.  Each Widget paints its own
//      visuals and recurses into its children.
//   4. The embedder routes mouse events to `root->hitTest(...)` to
//      find the topmost widget, then invokes that widget's
//      onLeftButtonDown / onMouseMove / onLeftButtonUp.
//
// Phase-1 stance (this file): Widget exists as the abstract type, but
// the legacy ResolvedWidget tree is still what Layout::expandLayout
// produces — see Layout.h's `using ResolvedWidget = …` alias.  Phase
// 2/3 migrate the per-tag paint paths off TreePainter::paintRecursive
// onto Widget subclasses.
//

#include <QHash>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

class QPainter;

namespace qtWasabi {

struct PaintCtx;
struct HitCtx;

class Widget {
public:
    // Tree shape — populated by the layout expander.  Same set of
    // fields ResolvedWidget historically held; the polymorphic
    // subclass adds per-widget behaviour, not extra data.
    QString tag;
    QString id;
    QString instanceId;
    QHash<QString, QString>               attrs;
    std::vector<std::unique_ptr<Widget>>  children;
    // Back-pointer to the owning parent in the resolved tree, set by
    // cacheResolvedRects on every layout pass.  Lets the Maki getParent()
    // binding return the real parent group (real Wasabi behaviour) instead
    // of the widget itself — e.g. centerlayer.m centers the branding layer
    // in its parent's width, which must be the holder, not the widget.
    Widget *parentWidget = nullptr;
    QString sourceFile;
    int     sourceLine = -1;

    virtual ~Widget() = default;

    // Factory: returns a fresh instance of the subclass that
    // implements `normalisedTag` (lowercased + `:` → `_`).  Falls
    // back to UnknownWidget when no class registered for the tag;
    // UnknownWidget paints nothing but still recurses children so
    // unrecognised tags don't break the tree.
    static std::unique_ptr<Widget> create(const QString &normalisedTag);

    // Case-insensitive lookup by `id` against the live widget tree.
    // Returns null when no widget with that id exists.  Used by
    // widgets that reference siblings — e.g. `<Menu normal=".." />`
    // looks up the three named buttons whose visibility it toggles.
    // Backed by SkinRuntimeBridge's `g_byId` registry, which is
    // populated as the tree is materialised.
    static Widget *findById(const QString &id);

    // Resolve x/y/w/h from `attrs` against the parent canvas,
    // honouring relatx/relaty/relatw/relath and the `fitparent`
    // shortcut.  Most widgets use the default; subclasses override
    // only for unusual sizing (e.g. Text auto-width from its display
    // string).
    virtual QRect resolveRect(const QSize &canvas) const;

    // Walk the tree top-down, resolving each widget's rect against the
    // actual canvas and caching the absolute result in lastCanvasRect.
    // This lets Maki getWidth()/getHeight() return EFFECTIVE pixel
    // sizes for relat-sized widgets (w="0" relatw="1") even before the
    // first paint or hit-test — e.g. Bento's `info.component.holder`
    // resolves to the real ~200px so fileinfo.maki's centerlayer macro
    // computes the WINAMP logo's x as (200-100)/2=50 instead of -50.
    // Called once after layout + before the script onResize dispatch.
    void cacheResolvedRects(QPoint origin, const QSize &canvas);

    // Single render entrypoint — every subclass overrides.  The
    // default recurses children with the parent canvas as a
    // container fallback; concrete widgets paint their own visuals
    // first and recurse afterwards (or not at all for leaves).
    virtual void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas);

    // Hit-test entrypoint — children-first depth search.  Returns
    // the topmost widget whose bbox (or painted alpha) contains
    // `point` (absolute canvas coords).  `origin` is the parent's
    // top-left in canvas coords; subclasses add their resolved
    // rect's x/y to it before recursing.  Returns null when no
    // widget matches.
    virtual Widget *hitTest(QPoint point, QPoint origin,
                            const QSize &canvas,
                            HitCtx &ctx, QRect *outBbox);

    // True for tags that translate their children into a local
    // coord space and shouldn't claim hits themselves.  Group,
    // Layout, Container, ComponentBucket, GroupXFade, the wasabi_*
    // xui groups — all override to return true.
    virtual bool isContainer() const { return false; }

    // True for widgets that are inherently interactive even without
    // an `id` attr (button, toggle, slider).  Used by the alpha-list
    // hit-test path's `requireIdOrInteractive` filter so unnamed
    // buttons inside a templated component still receive clicks.
    virtual bool isInteractive() const { return false; }

    // Canvas-space rect of the widget's own self-bbox, cached by
    // `Widget::hitTest` whenever this widget matches.  Widgets whose
    // event handlers need to translate canvas-relative click coords
    // back into widget-local coords (sliders, scrollbars, anything
    // with continuous dragging) read this — `attrs` only has the
    // PARENT-relative rect, which doesn't combine with the click
    // point in canvas coords.  Default-constructed when the widget
    // has never been hit-tested.
    QRect lastCanvasRect;

    // Wall-clock ms (QDateTime::currentMSecsSinceEpoch) when paint()
    // last ran for this widget.  Bumped by `MilkdropWidget` and any
    // `WindowHolderWidget` that hosts an AVS / video slot — lets
    // embedders detect "this widget is currently being painted"
    // independently of the visible attr (parent groups may be hidden
    // even when the leaf attr says visible=1).  Zero when the widget
    // has never been painted.  Widgets that don't need this leave it
    // at zero; cost is one int64 per widget.
    qint64 lastPaintedAtMs = 0;

    // Per-paint / per-hit-test child-origin shift applied INSIDE the
    // widget's resolved rect before recursing into children.
    // ComponentBucket overrides this to apply `-scroll * step`; the
    // Container family uses the default (no shift).  Kept on Widget
    // (not Container) so any future Widget that wants a scrolled
    // interior — TreeList, ScrollBar, future Popup — can override
    // without changing inheritance.
    virtual QPoint childOriginAdjustment() const { return QPoint(0, 0); }

    // Mouse / focus / animation events — defaults are no-ops.
    // Concrete interactive widgets (Slider, Button, ScrollBar)
    // override.  The embedder calls these only after hitTest has
    // identified the widget; they never run during paint.
    virtual void onLeftButtonDown (QPoint, PaintCtx &) {}
    virtual void onLeftButtonUp   (QPoint, PaintCtx &) {}
    virtual void onMouseMove      (QPoint, PaintCtx &) {}
    virtual void onMouseLeave     (PaintCtx &)         {}
    // Wheel notches under the cursor (+ = up/away, − = down/toward).
    virtual void onMouseWheel     (QPoint, int /*steps*/, PaintCtx &) {}
    virtual void onTargetReached  ()                   {}

    // True for widgets that need press→move→release capture (sliders,
    // scrollbar thumbs).  The embedder routes the press to this widget's
    // onLeftButtonDown and keeps it captured for move/up — and crucially
    // does NOT start a window drag.  Passive widgets (buttons fire on
    // click; backgrounds want the drag) return false.
    virtual bool capturesMouse() const { return false; }

    // True for widgets that are a SOLID rectangular interactive region —
    // every pixel inside the bbox is clickable, independent of painted
    // alpha.  List controls (the playlist / library holders) qualify:
    // real Winamp hosts them as an opaque child HWND, but qtWasabi paints
    // them "list-only" (transparent between rows) so the chrome shows
    // through, which would otherwise make the alpha hit-test reject a
    // click landing in the gap between two rows.  Default widgets stay
    // alpha-gated so transparent skin-bitmap regions click through.
    virtual bool isSolidHitRegion() const { return false; }

    // Called once after the XML attrs have been bulk-assigned into
    // `attrs` (right after `makeResolved` copies src.attrs).  Lets
    // subclasses register attr-driven side effects (e.g. ToggleButton
    // subscribing to its `cfgattrib` key on CfgAttribStore) without
    // overriding setXmlParam — the bulk assignment skips setXmlParam.
    // Default is a no-op.  Children are NOT yet expanded at this point.
    virtual void onAttrsInitialized() {}

    // Maki-script setXmlParam dispatch — Wasabi's runtime hook for
    // mutating widget attributes from script.  The default writes
    // the value to `attrs`; subclasses override to also keep typed
    // state members in sync (`ComponentBucket::m_scroll`, future
    // `Slider::m_dragOffset`, etc.).  Keeping the attrs hash as the
    // fallback storage means getXmlParam stays uniform — no per-
    // widget attribute name dispatch.
    virtual void setXmlParam(const QString &name, const QString &value);

    // Trigger a re-render from inside an event handler (e.g. after
    // a hover/press state flip).  Routes through SkinRuntimeBridge's
    // `g_repaint` — same callback `setXmlParam` already uses, set
    // by SkinView at init.  No-op when no embedder has registered
    // (offscreen tests still get visual change because they paint
    // synchronously from the snapshot, not via the repaint timer).
    static void requestRepaint();

    // Geometry resolution helper — the canonical implementation of
    // relatx/relaty/relatw/relath + fitparent semantics, used by
    // every subclass's `resolveRect` AND by external callers that
    // need the same formula (Layout's window-region builder, future
    // Phase-7 unit tests).  Public so it can be consumed outside the
    // Widget hierarchy.
    static QRect resolveRectFromAttrs(
        const QHash<QString, QString> &attrs, QSize canvas);
};

}  // namespace qtWasabi
