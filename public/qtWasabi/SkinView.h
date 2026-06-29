#pragma once
//
// SkinView — a QWidget that paints a qtWasabi resolved layout.
//
// Embedders create one of these per <container>/<layout> they want
// on screen, hand it the parsed Document and the ids, and the widget
// owns the BitmapRegistry + ResolvedWidget tree from there on.  The
// `paint()` is just a TreePainter call on its tree; the size hint
// comes from the layout's `w`/`h` (or `minimum_w`/`minimum_h`).
//
// Input handling mirrors the main window: a press on a capture-style
// widget (slider/scrollbar) routes press→move→release; a press on a
// passive widget fires its onLeftClick / action; a press on the bare
// titlebar/chrome drags the frameless toplevel.  This is what makes
// the Playlist Editor, Media Library, and detached Video/Visualizer
// subwindows draggable by their titlebar and their buttons clickable.
//

#include <qtWasabi/Layout.h>
#include <qtWasabi/BitmapRegistry.h>
#include <qtWasabi/ColorRegistry.h>
#include <qtWasabi/FontRegistry.h>
#include <qtWasabi/GammasetRegistry.h>

#include <QList>
#include <QRegion>
#include <QString>
#include <QWidget>
#include <functional>

class QVariantAnimation;
class QMouseEvent;

namespace qtWasabi::SkinXml { struct Document; }

namespace qtWasabi {

class Host;
class SkinRuntime;
struct PaintCtx;

class SkinView : public QWidget {
    Q_OBJECT
public:
    explicit SkinView(QWidget *parent = nullptr);
    ~SkinView() override;

    // Adopt a parsed skin document, expand the named layout, and
    // populate the bitmap registry from it.  Returns false (and sets
    // errMsg) if the container or layout isn't found.
    bool load(const SkinXml::Document &doc,
              const QString &containerId,
              const QString &layoutId = QStringLiteral("normal"),
              QString *errMsg = nullptr);

    // The native size of the loaded layout (w x h from XML, falling
    // back to minimum_w x minimum_h, or sizeHint default).
    QSize layoutNativeSize() const { return m_nativeSize; }

    // Access the parsed tree, e.g. for hit-testing.
    const Layout::ResolvedWidget &tree() const { return m_tree; }
    BitmapRegistry               &registry()   { return m_registry; }
    FontRegistry                 &fonts()      { return m_fonts; }
    ColorRegistry                &colors()     { return m_colors; }
    GammasetRegistry             &gammasets()  { return m_gammasets; }

    // Switch to a named gammaset (Color Theme).  Empty/unknown name
    // means "Default" (identity transform).  Triggers a repaint.
    void setActiveGammaset(const QString &name);

    // Re-run computeWindowRegion against the current tree.  Call
    // this after mutating widget positions (e.g. a static
    // runKnownScripts pass that moves a drawer) so the region
    // mask stays in sync with where the chrome actually paints.
    // load() does an initial compute itself.
    void rebuildWindowRegion();

    // Update the layout's native size — used by Maki Layout.setTarget*
    // + gotoTarget chains (drawer scripts that grow the window when
    // expanding).  Resizes the widget, syncs the layout root's w/h
    // attrs so relatw/relath children re-flow, and recomputes the
    // window region against the new bounds.
    void resizeLayoutTo(const QSize &size);

    // Tween the layout size from current to `size` over `durationMs`.
    // Pumps resizeLayoutTo on each animation tick (60 fps via
    // QVariantAnimation) and fires Maki `onTargetReached` on
    // completion via `fireTargetReached()`.  Mirrors real Wasabi's
    // gotoTarget-with-setTargetSpeed semantics.  Embedders register
    // this as their skin-resize callback when they want animated
    // drawer / window transitions.
    void animatedResizeLayoutTo(const QSize &size, int durationMs = 200);

    // The currently-computed window region (for embedders that
    // override paintEvent and want to apply the same clip).
    const QRegion &windowRegion() const { return m_windowRegion; }

    // Auto-shrink the QWidget to the painted-region's bounding box
    // after every rebuildWindowRegion.  When a drawer hides itself
    // by moving off-screen (configtabs's `drawer.setXmlParam("y",
    // "-263")` chain), the layout's native size stays the same but
    // the visible chrome ends earlier.  Without auto-shrink the OS
    // window keeps its full size and the bottom transparent area
    // bleeds through to the desktop.  Off by default to preserve
    // explicit Maki-driven layout sizes.
    void setAutoShrinkToRegion(bool on) { m_autoShrink = on; }
    bool autoShrinkToRegion() const     { return m_autoShrink; }

    // Alpha-aware hit-test: returns the topmost widget at the point
    // whose painted alpha is non-zero.  Walks the resolved tree in
    // paint-order-reverse (topmost first) and checks each widget's
    // painted pixels via the alpha cache populated during paintEvent.
    // Falls back to Layout::hitTest when the alpha cache hasn't been
    // populated yet (first frame).  Replaces qtamp's deep-walk click
    // dispatch fallback: clicks now reach the visually-frontmost
    // widget that actually paints an opaque pixel at the click point,
    // even when chrome layers above are transparent there.
    //
    // The image-size resolver lets sized-by-image widgets (e.g. togglebuttons
    // with just `image=`) resolve their bbox from the bitmap dimensions.
    // Pass nullptr when only explicit w/h widgets matter.
    const Layout::ResolvedWidget *
    alphaHitTest(QPoint pointInLayout, bool actionOnly = false,
                 Layout::ImageSizeResolver imageSize = nullptr,
                 void *imageSizeUserdata = nullptr) const;

    // Return every widget at `pointInLayout` whose painted alpha is
    // non-zero, ordered topmost-first.  Used to model Wasabi's
    // event-bubbling: when the topmost-opaque widget has no Maki
    // handler bound (e.g. a chrome corner), the click can be
    // re-tried on the next z-down widget.  This generalises the
    // old per-skin deep-walk fallback.
    QList<const Layout::ResolvedWidget *>
    alphaHitTestList(QPoint pointInLayout, bool actionOnly = false,
                     Layout::ImageSizeResolver imageSize = nullptr,
                     void *imageSizeUserdata = nullptr) const;

    // Subclasses that override paintEvent (e.g. qtamp's player window
    // that paints with extra state like the ColorThemes list cache)
    // must call this from their paintEvent so alphaHitTest sees the
    // same frame the user is looking at.  Ownership: the QImage is
    // copied/moved into our cache.  Also drives auto-shrink when
    // enabled — the painted alpha is the authoritative source for
    // visible extent (sysregion alone isn't, because some skins use
    // a sysregion rectangle larger than what their bitmaps actually
    // cover).
    void setPaintedAlpha(QImage img);

    // Embedder hook: resolve a <text display="…"/> key to a live
    // string at paint time.  Returning an empty string falls back
    // to the widget's `default=` attribute.
    using DisplayResolver = std::function<QString(const QString &)>;
    void setDisplayResolver(DisplayResolver r) {
        m_resolver = std::move(r);
        update();
    }

    // Bind an embedder Host so paintEvent pulls live display
    // strings + slider thumb positions straight from it.  Takes
    // precedence over a manual setDisplayResolver().  Pass nullptr
    // to detach.
    void  setHost(Host *h) { m_host = h; update(); }
    Host *host() const     { return m_host; }

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    QSize sizeHint() const override { return m_nativeSize; }

private:
    // Route a left-click at `p` through onLeftClick + onAction bubbling,
    // exactly like the main window's dispatchClickAt: try the topmost
    // alpha-opaque widget, walk its parent chain for a script handler,
    // then fall back to its `action=` verb.  Returns the consuming id,
    // or empty when nothing claimed it (→ the press becomes a drag).
    QString dispatchClickAt(QPoint p);
    // Build a PaintCtx wired to this view's registries + host so a
    // captured widget (slider/scrollbar) reads/writes exactly as paint.
    PaintCtx makeEventCtx();

    Layout::ResolvedWidget m_tree;
    BitmapRegistry         m_registry;
    ColorRegistry          m_colors;
    FontRegistry           m_fonts;
    GammasetRegistry       m_gammasets;
    QSize                  m_nativeSize { 354, 280 };
    DisplayResolver        m_resolver;
    Host                  *m_host = nullptr;
    QRegion                m_windowRegion;
    // Painted-alpha cache for alpha-aware hit-test.  Captured during
    // paintEvent (the same QImage we render to gets stored alpha-only)
    // and sampled by alphaHitTest().  One image per skin paint —
    // hit-tests check pixels directly without re-rendering.
    mutable QImage         m_paintedAlpha;
    // Owned by animatedResizeLayoutTo; lazily created.  Tweens the
    // layout's native size.  Held as a child QObject so it auto-
    // destructs with the view.  QVariantAnimation header isn't
    // included here; pulled in by the .cpp.
    QVariantAnimation     *m_resizeAnim = nullptr;
    // setAutoShrinkToRegion toggle.
    bool                   m_autoShrink = false;
    // This subwindow's OWN Maki runtime (per-window).  Created in load()
    // and scoped to this view's tree root so its scripts (menu layout,
    // drawers, …) run through the VM without clobbering the player's
    // registration.  Owned; torn down in the destructor.
    SkinRuntime           *m_runtime = nullptr;
    // Capture-style widget (slider/scrollbar) holding the current
    // press for press→move→release dragging; null when none.
    Widget                *m_activeWidget = nullptr;
    // Manual window-drag fallback (used when the compositor's
    // startSystemMove() isn't available): tracks the toplevel origin.
    bool                   m_dragging = false;
    QPoint                 m_dragOriginGlobal;
    QPoint                 m_dragWindowStart;
    // Manual edge-resize (frameless subwindows have no native border;
    // the compositor declines startSystemResize on Wayfire).
    Qt::Edges              m_resizeEdges;
    QRect                  m_resizeStartGeom;
    QPoint                 m_resizeOriginGlobal;
};

}  // namespace qtWasabi
