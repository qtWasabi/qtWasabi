#pragma once
//
// PaintCtx — per-paint context threaded through every Widget::paint
// call.  Promoted to a public header (was file-local inside
// TreePainter.cpp) so the per-widget classes under src/widgets/ can
// declare their paint signatures without re-including the monolithic
// TreePainter translation unit.
//
// All pointers are non-owning.  The PaintCtx is constructed at the
// top of each updatePaintNode pass and torn down at the end; widget
// classes must not retain any of these pointers across frames.
//
// Mirrors what `paintRecursive`'s file-local PaintCtx historically
// passed.  Widget-type-specific extras (colorthemes selection rows,
// vis mode) stay here as opt-in fields rather than forcing widgets
// that don't care to forward them via separate parameters.
//

#include <QHash>
#include <QString>
#include <QRect>
#include <functional>

namespace qtWasabi {

class BitmapRegistry;
class ColorRegistry;
class FontRegistry;
class GammasetRegistry;
class Host;

// `<text display="…"/>` and `<songticker>` lookup hook.  Embedders
// translate display keys ("songtitle", "kbps", "channels", …) to
// the live string to render.  Returns empty string when the key is
// unknown — TextPainter falls back to the widget's `default=` attr.
using DisplayResolver = std::function<QString(const QString &)>;

struct PaintCtx {
    BitmapRegistry        *bmp = nullptr;
    FontRegistry          *font = nullptr;
    DisplayResolver        resolver;
    Host                  *host = nullptr;
    GammasetRegistry      *gammasets = nullptr;
    ColorRegistry         *colors = nullptr;

    // Colour-themes list state (read by ColorThemesListWidget, written
    // back via the *Out pointers so the embedder can persist scroll
    // position / record the on-screen bbox for hit-test).
    int                    colorthemesSelected = 0;
    int                    colorthemesTopRow = 0;
    QRect                 *colorthemesBboxOut = nullptr;
    int                   *colorthemesTopRowOut = nullptr;

    // Visualisation mode (0=Off, 1=Spectrum, 2=Oscilloscope, 3=VU).
    // Read by VisWidget; embedder picks via right-click submenu.
    int                    visMode = 1;

    // Window focus state for the active/inactive layer convention.  Real
    // Winamp blits each layer at `isActive() ? activealpha : inactivealpha`;
    // skins ship `.active`/`.inactive` titlebar (and other) pairs gated on
    // those attrs.  The host sets this false for non-focused windows (the
    // container subwindows in a multi-window screenshot) so their chrome
    // renders the dim inactive variant.  Default true (the focused player).
    bool                   windowActive = true;
};

}  // namespace qtWasabi
