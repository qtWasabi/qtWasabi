#pragma once
//
// <layer> — Wasabi's primitive bitmap blit.  Most chrome and most
// non-interactive widgets are composed by stacking layers; this is
// the workhorse paint operation.
//
// LayerWidget delegates the actual blit to `LayerPainter::paintLayer`
// (which handles relat-positioning, alpha compositing, and the
// gammaset transform) and adds four special-cases on top:
//
//   1. `image="wasabi.scrollbar.vertical.button"` on a layer that
//      lives inside the colour-themes drawer — the layer's `y` is
//      overridden every paint to mirror the list's current `topRow`,
//      so the thumb tracks the visible scroll position.
//   2. Mono/stereo channel indicator — `id="mono"` and `id="stereo"`
//      layers' `image="…inactive"` are swapped to `"…active"` based
//      on the host's current channel count (replicates monoster.maki
//      so the lit indicator works without scripting).
//   3. Sysregion cutout-mask layers (`sysregion="-1"` / `"-2"`) are
//      pure-alpha bitmaps that ONLY contribute to the window region
//      computation; painting them onto the visible buffer would lay
//      down BLACK pixels under the rounded-corner cutouts.  Skip.
//   4. Volumebar live width — `id="volumebar"` is normally driven by
//      `volume.maki`, but until the runtime drives it, derive `w`
//      from the host's volume slider position so the fill matches
//      the thumb.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class LayerWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace WasabiQt
