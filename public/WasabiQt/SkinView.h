#pragma once
//
// SkinView — a QWidget that paints a WasabiQt resolved layout.
//
// Embedders create one of these per <container>/<layout> they want
// on screen, hand it the parsed Document and the ids, and the widget
// owns the BitmapRegistry + ResolvedWidget tree from there on.  The
// `paint()` is just a TreePainter call on its tree; the size hint
// comes from the layout's `w`/`h` (or `minimum_w`/`minimum_h`).
//
// Input handling is a stub for now — clicks/drags are reported via
// signals but the widget doesn't dispatch them to script bindings
// (that's M9+'s job).
//

#include <WasabiQt/Layout.h>
#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/ColorRegistry.h>
#include <WasabiQt/FontRegistry.h>
#include <WasabiQt/GammasetRegistry.h>

#include <QList>
#include <QRegion>
#include <QString>
#include <QWidget>
#include <functional>

namespace WasabiQt::SkinXml { struct Document; }

namespace WasabiQt {

class Host;

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

    // Access the parsed tree, e.g. for hit-testing in M9.
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

    // The currently-computed window region (for embedders that
    // override paintEvent and want to apply the same clip).
    const QRegion &windowRegion() const { return m_windowRegion; }

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
    // copied/moved into our cache.
    void setPaintedAlpha(QImage img) { m_paintedAlpha = std::move(img); }

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
    QSize sizeHint() const override { return m_nativeSize; }

private:
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
};

}  // namespace WasabiQt
