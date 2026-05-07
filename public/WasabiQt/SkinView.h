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
#include <WasabiQt/FontRegistry.h>
#include <WasabiQt/GammasetRegistry.h>

#include <QString>
#include <QWidget>
#include <functional>

namespace WasabiQt::SkinXml { struct Document; }

namespace WasabiQt {

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
    GammasetRegistry             &gammasets()  { return m_gammasets; }

    // Switch to a named gammaset (Color Theme).  Empty/unknown name
    // means "Default" (identity transform).  Triggers a repaint.
    void setActiveGammaset(const QString &name);

    // Embedder hook: resolve a <text display="…"/> key to a live
    // string at paint time.  Returning an empty string falls back
    // to the widget's `default=` attribute.
    using DisplayResolver = std::function<QString(const QString &)>;
    void setDisplayResolver(DisplayResolver r) {
        m_resolver = std::move(r);
        update();
    }

protected:
    void paintEvent(QPaintEvent *e) override;
    QSize sizeHint() const override { return m_nativeSize; }

private:
    Layout::ResolvedWidget m_tree;
    BitmapRegistry         m_registry;
    FontRegistry           m_fonts;
    GammasetRegistry       m_gammasets;
    QSize                  m_nativeSize { 354, 280 };
    DisplayResolver        m_resolver;
};

}  // namespace WasabiQt
