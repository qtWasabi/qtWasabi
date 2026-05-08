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
#include <QSize>

class QPainter;

namespace WasabiQt {

class BitmapRegistry;
namespace Layout { struct ResolvedWidget; }

namespace TreePainter {

// Paint every visible widget in `root` onto `p`, using `reg` for
// bitmap lookups.  `canvas` is the pixel size of the area `root`
// occupies — used to resolve relat*/negative-w-h.
void paintTree(QPainter *p, const Layout::ResolvedWidget &root,
               BitmapRegistry &reg, const QSize &canvas);

}  // namespace TreePainter
}  // namespace WasabiQt
