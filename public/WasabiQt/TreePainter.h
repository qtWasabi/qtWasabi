#pragma once
//
// TreePainter — walks a WasabiQt::Layout::ResolvedWidget tree and
// paints every supported widget kind with one QPainter pass.
//
// What's supported in M7:
//
//   <layer>        — bitmap blit (delegates to LayerPainter)
//   <button>       — paints the `image` (normal-state) bitmap;
//                    pressed/hover states are input-time concerns
//                    handled by the embedder, not the static painter
//   <togglebutton> — same as <button>; toggle state lives in the host
//   <group>        — recurses with a translated origin
//   <container>    — same as <group> for paint purposes
//   <layout>       — same; the root the embedder hands us
//
// What's not yet supported (deliberately stubbed, not painted):
//
//   <text>         — needs bitmap font + font-loader
//   <slider>       — needs slider geometry resolver
//   <vis>          — needs spectrum/waveform data from the host
//   <albumart>     — needs media-info backend
//
// `visible="0"` widgets are skipped.  `relatx`/`relaty`/`relatw`/`relath`
// are resolved against the parent's pixel size at paint time.

#include <QtCore/qglobal.h>
#include <QRect>
#include <QSize>
#include <functional>
#include <QString>

class QPainter;

namespace WasabiQt {

class BitmapRegistry;
class FontRegistry;
class ColorRegistry;
class GammasetRegistry;
class Host;
namespace Layout { struct ResolvedWidget; }

namespace TreePainter {

// Optional callback the embedder supplies to resolve a <text>
// widget's `display=` key into a live string.  Empty result falls
// back to the `default=` attribute.
using DisplayResolver = std::function<QString(const QString &)>;

// Paint every visible widget in `root` onto `p`, using `reg` for
// bitmap lookups and `fontReg` for <text> widgets.  `canvas` is the
// pixel size of the area `root` occupies.  `resolver` is optional.
void paintTree(QPainter *p, const Layout::ResolvedWidget &root,
               BitmapRegistry &reg, FontRegistry &fontReg,
               const QSize &canvas,
               const DisplayResolver &resolver = {});

// Same as above but driven by a Host pointer — paintTree pulls
// display strings and slider positions straight from `host`.  Use
// this overload to get <slider> thumbs painted at their live audio
// position; the DisplayResolver overload falls back to no-thumb.
void paintTree(QPainter *p, const Layout::ResolvedWidget &root,
               BitmapRegistry &reg, FontRegistry &fontReg,
               const QSize &canvas, Host *host);

// Same again, but also passes a GammasetRegistry pointer so the
// painter can render Wasabi widgets that enumerate available colour
// themes (`<ColorThemes:List>`) and highlight the active one.
void paintTree(QPainter *p, const Layout::ResolvedWidget &root,
               BitmapRegistry &reg, FontRegistry &fontReg,
               const QSize &canvas, Host *host,
               GammasetRegistry *gammasets,
               ColorRegistry *colors,
               int colorthemesSelectedRow,
               int colorthemesTopRowIn,
               QRect *colorthemesListBboxOut,
               int  *colorthemesTopRowOut,
               int  visMode = 1);   // 0=off, 1=spectrum, 2=osc, 3=VU

}  // namespace TreePainter
}  // namespace WasabiQt
