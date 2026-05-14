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

namespace WasabiQt {

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
    QString sourceFile;
    int     sourceLine = -1;

    virtual ~Widget() = default;

    // Factory: returns a fresh instance of the subclass that
    // implements `normalisedTag` (lowercased + `:` → `_`).  Falls
    // back to UnknownWidget when no class registered for the tag;
    // UnknownWidget paints nothing but still recurses children so
    // unrecognised tags don't break the tree.
    static std::unique_ptr<Widget> create(const QString &normalisedTag);

    // Resolve x/y/w/h from `attrs` against the parent canvas,
    // honouring relatx/relaty/relatw/relath and the `fitparent`
    // shortcut.  Most widgets use the default; subclasses override
    // only for unusual sizing (e.g. Text auto-width from its display
    // string).
    virtual QRect resolveRect(const QSize &canvas) const;

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
    virtual void onTargetReached  ()                   {}

    // Maki-script setXmlParam dispatch — Wasabi's runtime hook for
    // mutating widget attributes from script.  The default writes
    // the value to `attrs`; subclasses override to also keep typed
    // state members in sync (`ComponentBucket::m_scroll`, future
    // `Slider::m_dragOffset`, etc.).  Keeping the attrs hash as the
    // fallback storage means getXmlParam stays uniform — no per-
    // widget attribute name dispatch.
    virtual void setXmlParam(const QString &name, const QString &value);

protected:
    // Helper shared by every subclass that doesn't override
    // resolveRect — same implementation Layout::resolveRect /
    // TreePainter::resolveRect use today, hoisted here so the same
    // formula resolves geometry in paint, hit-test, region build.
    static QRect resolveRectFromAttrs(
        const QHash<QString, QString> &attrs, QSize canvas);
};

}  // namespace WasabiQt
