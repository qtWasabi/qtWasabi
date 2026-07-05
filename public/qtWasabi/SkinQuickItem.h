#pragma once
//
// SkinQuickItem — Qt Quick / Scene Graph renderer for a Wasabi skin.
//
// Replaces the QPainter-based SkinView/TreePainter/LayerPainter/TextPainter
// stack with a single custom QQuickItem that builds a QSGNode tree from
// the resolved widget tree.  Hosted in a QQuickWindow (frameless,
// transparent) by the embedder.  Reuses the same Host abstraction,
// SkinXml parser, SkinRuntime/Maki VM, BitmapRegistry, FontRegistry,
// ColorRegistry, and GammasetRegistry as the old SkinView.
//
// Why the switch: alpha-aware hit-test (override contains()), real
// animations (QPropertyAnimation on per-widget properties), correct
// Wayland window shape via wl_surface.set_input_region (via
// QWindow::setMask), and a path to render every Wasabi widget tag
// without per-skin glue.
//
// API mirrors SkinView's surface so embedders can migrate incrementally.
//

#include <qtWasabi/BitmapRegistry.h>
#include <qtWasabi/PaintCtx.h>
#include <qtWasabi/ColorRegistry.h>
#include <qtWasabi/FontRegistry.h>
#include <qtWasabi/GammasetRegistry.h>
#include <qtWasabi/Layout.h>

#include <QHash>
#include <QImage>
#include <QList>
#include <QPointer>
#include <QQuickItem>
#include <QRect>
#include <QSize>
#include <QString>
#include <functional>

class QVariantAnimation;

namespace qtWasabi::SkinXml { struct Document; }

namespace qtWasabi {

class Host;

class SkinQuickItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QSize layoutNativeSize READ layoutNativeSize NOTIFY
               layoutNativeSizeChanged)
public:
    explicit SkinQuickItem(QQuickItem *parent = nullptr);
    ~SkinQuickItem() override;

    // Adopt a parsed skin document, expand the named layout, and
    // populate the bitmap registry from it.  Returns false (and sets
    // errMsg) if the container or layout isn't found.  The document
    // is stashed by const-pointer (NOT copied), so the caller must
    // keep it alive for the SkinQuickItem's lifetime (qtamp's
    // QtampPlayerWindow owns the doc and outlives the item).
    bool load(const SkinXml::Document &doc,
              const QString &containerId,
              const QString &layoutId = QStringLiteral("normal"),
              QString *errMsg = nullptr);

    const SkinXml::Document *document() const { return m_doc; }

    // The native size of the loaded layout (w x h from XML, falling
    // back to minimum_w x minimum_h, or default).
    QSize layoutNativeSize() const { return m_nativeSize; }

    // Display render ratio, the reference basewnd::setRenderRatio model:
    // the layout keeps its XML-unit coordinates, the painted output and
    // the toplevel window scale by this factor.  Winamp exposes it as
    // doublesize / the scale submenu; skins drive it via Maki setScale.
    // Initialized from WASABIQT_RENDER_RATIO (a user-state instrument,
    // e.g. the corpus harness reproducing an author's 50% screenshot).
    double renderRatio() const { return m_renderRatio; }
    void   setRenderRatio(double r);
    QSize  displaySize() const {
        return { int(m_nativeSize.width()  * m_renderRatio + 0.5),
                 int(m_nativeSize.height() * m_renderRatio + 0.5) };
    }

    // Access the parsed tree + asset registries.
    const Layout::ResolvedWidget &tree() const { return m_tree; }
    BitmapRegistry               &registry()   { return m_registry; }
    FontRegistry                 &fonts()      { return m_fonts; }
    ColorRegistry                &colors()     { return m_colors; }
    GammasetRegistry             &gammasets()  { return m_gammasets; }

    // Switch to a named gammaset (Color Theme).  Empty/unknown name
    // means "Default" (identity transform).  Triggers a repaint.
    // Virtual so an embedder can react to a theme change — e.g. re-tint
    // its own (non-skin) chrome to match the newly active theme.
    virtual void setActiveGammaset(const QString &name);

    // Resize the layout to a new size (e.g. from a Maki Layout.setTarget*
    // / gotoTarget chain).  Updates m_nativeSize, syncs the layout root
    // w/h attrs so relatw/relath children re-flow, and queues a paint.
    void resizeLayoutTo(const QSize &size);

    // Same as resizeLayoutTo but tweens from current to target over
    // durationMs using QVariantAnimation; fires fireTargetReached()
    // on completion.  Mirrors SkinView's identically-named helper.
    void animatedResizeLayoutTo(const QSize &target, int durationMs = 200);

    // Embedder hook: resolve a <text display="…"/> key to a live string
    // at paint time.  Returning an empty string falls back to the
    // widget's `default=` attribute.
    using DisplayResolver = std::function<QString(const QString &)>;
    void setDisplayResolver(DisplayResolver r) {
        m_resolver = std::move(r);
        update();
    }

    // Bind an embedder Host so paint pulls live display strings + slider
    // thumb positions straight from it.  Takes precedence over a manual
    // setDisplayResolver.  Pass nullptr to detach.
    void  setHost(Host *h) { m_host = h; update(); }
    Host *host() const     { return m_host; }

    // Alpha-aware hit-test: returns the topmost widget at `pointInLayout`
    // whose painted alpha at that point is non-zero.  Falls back to a
    // bbox-only topmost match when the painted-alpha cache hasn't been
    // populated yet (first frame).  Used by qtamp's mousePressEvent in
    // place of the prior deep-walk fallback.
    const Layout::ResolvedWidget *
    topmostWidgetAt(QPoint pointInLayout, bool actionOnly = false) const;

    // Like topmostWidgetAt but returns EVERY opaque-at-point widget in
    // z-order (topmost first).  Caller iterates and tries Maki
    // fireWidgetEvent on each — first one whose handler dispatches
    // consumes the click.  Generalises the deep-walk pattern for
    // chrome layers without script bindings.
    QList<const Layout::ResolvedWidget *>
    alphaHitTestList(QPoint pointInLayout, bool actionOnly = false,
                     Layout::ImageSizeResolver imageSize = nullptr,
                     void *imageSizeUserdata = nullptr) const;

    // Re-run computeWindowRegion against the current tree.  Embedders
    // call this after mutating widget positions / visibility so the
    // QQuickWindow's input-region mask stays in sync with what the
    // chrome actually paints.  Loading the skin already does it once.
    void rebuildWindowRegion();
    // Coalesced variant: marks the region dirty and rebuilds at most
    // once when control returns to the event loop.  A Maki onTimer can
    // fire dozens of setAttr per tick; computeWindowRegion is a full
    // re-render + per-pixel alpha scan, so doing it synchronously per
    // setAttr saturates the GUI thread (dead clicks / no hover).  This
    // collapses a burst to one rebuild, still on the GUI thread and
    // after the script dispatch (so post-script attr values are used).
    void scheduleRegionRebuild();
    const QRegion &windowRegion() const { return m_windowRegion; }

    // Auto-shrink the QQuickWindow to the painted-region's bounding
    // box after every repaint.  Same idea as SkinView — when a Maki
    // mutation slides a drawer off-screen the layout's native size
    // stays the same but the visible chrome ends earlier; auto-shrink
    // resizes the window so the desktop doesn't bleed through.  Off
    // by default; qtamp opts in.
    void setAutoShrinkToRegion(bool on) { m_autoShrink = on; }
    bool autoShrinkToRegion() const     { return m_autoShrink; }

    // Fire `onLeftClick` Maki handlers along the alpha-hit-list at the
    // given local point; returns the id that consumed the click (empty
    // if nothing did).  Convenience for QML embedders that want the
    // canonical Wasabi click dispatch without re-implementing the
    // walk.  Public so MouseArea handlers can call it.
    Q_INVOKABLE QString dispatchClickAt(QPointF localPoint);

    // Test hook: run a full left press+release through the REAL handlers
    // (mousePressEvent / mouseReleaseEvent), exactly as a live click does —
    // offscreen QQuickItems don't receive synthesised platform mouse events,
    // so this is how offscreen tests exercise the true click path.
    Q_INVOKABLE void testClick(QPointF localPoint);

    // Re-point the borrowed document pointer.  Used by subclasses
    // that take ownership of the parsed Document (QtampPlayerWindow
    // moves it into a member) so the base-class m_doc — captured by
    // `load(doc, ...)` from the caller's local — gets updated to
    // point at the now-owned-by-subclass copy.  Without this fix
    // reloadSkin leaves m_doc dangling: load() captures the address
    // of reloadSkin's local doc, setSkinDocument() moves it into a
    // member, reloadSkin returns and the local is destroyed — and
    // the next paint dereferences the dangling m_doc in
    // resolveGroupXFadePages.
    void setDocument(const SkinXml::Document *doc) { m_doc = doc; }

protected:
    // Scene Graph entry point — called on the GUI thread once per
    // frame whenever update() has been queued.  We build a fresh node
    // tree each call; texture caching is owned by the QQuickWindow.
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;

    // Paint hook — subclasses override to pass extra state to
    // TreePainter (gammasets, colorthemes selection, vis mode).
    // Default implementation paints with just Host (or resolver).
    // Called from updatePaintNode into a transparent QImage buffer;
    // the result is uploaded as a QSGTexture.
    virtual void paintInto(QPainter *p, const QSize &canvas);

    // Alpha-aware QQuickItem hit-test.  Walks the resolved tree at the
    // local point and asks the cached painted-alpha map whether the
    // widget at that point is opaque.  This makes the chrome layers'
    // transparent areas correctly pass clicks through to the widgets
    // visually behind them.
    bool contains(const QPointF &point) const override;

    // Route mouse press through the alpha-hit-list + Maki onLeftClick
    // dispatch.  Empty-area clicks initiate a window drag via
    // QWindow::startSystemMove on Wayland (or a manual setPosition
    // fallback elsewhere).
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent (QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    // Wheel routing — dispatches scroll notches into the widget under the
    // cursor (onMouseWheel), so list-style holders (playlist / library)
    // and any scroll-aware widget scroll generically on every skin.
    void wheelEvent(QWheelEvent *e) override;

    // Begin a window resize if localPos is within the edge/corner grab
    // margin of the (frameless) toplevel.  Tries the compositor's native
    // resize first; if that's refused (Wayfire) it arms the manual
    // setGeometry fallback that mouseMoveEvent applies.  Returns true if a
    // resize was started and the caller should consume the press.  Exposed
    // so embedder subclasses that override mousePressEvent (and handle
    // window-move themselves) can offer edge-resize before their own move.
    bool beginEdgeResize(const QPointF &localPos, const QPoint &globalPos);

    // Build a PaintCtx wired to this item's registries/host, used to route
    // mouse events into capture widgets (sliders) the same way paint does.
    qtWasabi::PaintCtx makeEventCtx();

    // Hover-event routing — dispatches Qt hover events into the
    // widget tree's virtual chain so ButtonWidget / MenuWidget /
    // etc. can paint their `hoverImage=` variants when the cursor
    // enters their bbox.  topmostWidgetAt finds the current widget;
    // m_hoverWidget tracks the previous so move-to-different-widget
    // fires onMouseLeave on the old before onMouseMove on the new.
    void hoverMoveEvent (QHoverEvent *e) override;
    void hoverLeaveEvent(QHoverEvent *e) override;

signals:
    void layoutNativeSizeChanged();

private:
    Layout::ResolvedWidget m_tree;
    BitmapRegistry         m_registry;
    ColorRegistry          m_colors;
    FontRegistry           m_fonts;
    GammasetRegistry       m_gammasets;
    QSize                  m_nativeSize { 354, 280 };
    DisplayResolver        m_resolver;
    Host                  *m_host = nullptr;

    // Painted-alpha cache used by contains() — populated when we paint
    // each widget.  Keyed on ResolvedWidget* pointer (stable for the
    // tree's lifetime).  Each entry is a tiny grayscale alpha buffer
    // at the widget's painted size.
    mutable QHash<const Layout::ResolvedWidget *, QImage> m_alphaCache;
    // Drag state for empty-area window move (titlebar drag).
    bool   m_dragging = false;
    QPoint m_dragOriginGlobal;
    QPoint m_dragWindowStart;
    // Manual window-resize fallback for when QWindow::startSystemResize is
    // unsupported by the compositor (Wayfire returns false → the press would
    // otherwise fall through to a window MOVE).  Records the grabbed edges,
    // the window geometry and the global cursor at press; mouseMoveEvent then
    // setGeometry's per edge.  Wayland honours size-only changes, so the
    // bottom/right edges (and the bottom-right corner) resize correctly.
    Qt::Edges m_resizeEdges;
    QRect     m_resizeStartGeom;
    QPoint    m_resizeOriginGlobal;
    // Widget currently under the mouse cursor (for hover state).
    // Set by hoverMoveEvent; cleared by hoverLeaveEvent and by
    // move-to-different-widget transitions.
    qtWasabi::Widget *m_hoverWidget = nullptr;
    // Widget that captured the current press (slider/scrollbar thumb).
    // Set in mousePressEvent when capturesMouse(); receives onMouseMove
    // until mouseReleaseEvent fires its onLeftButtonUp + clears it.  While
    // non-null, no window drag/resize is started.
    qtWasabi::Widget *m_activeWidget = nullptr;
    // Accumulates fractional wheel deltas: Wayland/libinput delivers
    // high-resolution sub-notch angleDelta (e.g. 95, not 120), so a plain
    // /120 floors every event to zero steps.  We sum until a full 120-unit
    // notch is reached, emit the step(s), and keep the remainder.
    int m_wheelAccumY = 0;
    // Cached window region from the last rebuildWindowRegion call.
    QRegion m_windowRegion;
    // Same region in layout units (pre-renderRatio scale) — the paint
    // buffer works in layout units, the published m_windowRegion in
    // display pixels.
    QRegion m_windowRegionLayout;
    // Auto-shrink to painted extent toggle (off by default).
    bool   m_autoShrink = false;
    double m_renderRatio = 1.0;
    // Floor for the auto-shrink: the layout's declared minimum_h, so a
    // transient paint (e.g. a drawer close that momentarily hides docked
    // content) can never collapse the window below the skin's valid
    // minimum height.  0 = no floor.
    int    m_minShrinkH = 0;
    // Lazily-created resize-tween animation (parented to this).
    QVariantAnimation *m_resizeAnim = nullptr;
    // True while m_resizeAnim is running.  Suppresses the per-frame
    // QQuickWindow resize inside resizeLayoutTo — animatedResizeLayoutTo
    // pre-grows the window to max(from, target) once, then lets the
    // animation update m_nativeSize without re-triggering Wayland
    // surface reconfigures every tick.
    bool   m_layoutAnimActive = false;
    // First setMask deferred via a 150 ms one-shot timer so Wayfire's
    // first wl_surface.commit lands BEFORE setMask is applied.
    bool   m_maskInitialised = false;
    // Set when scheduleRegionRebuild has a queued rebuild pending, so a
    // burst of setAttr calls coalesces to a single region recompute.
    bool   m_regionRebuildPending = false;
    // Borrowed (not owned) pointer to the parsed skin document — used
    // by the per-paint GroupXFade page-resolution pass to instantiate
    // a groupdef into a target widget by id at runtime.
    const SkinXml::Document *m_doc = nullptr;
};

}  // namespace qtWasabi
