#pragma once
//
// <vis> — Wasabi visualisation widget.  Renders spectrum bars,
// oscilloscope, or VU meter based on `PaintCtx::visMode` (driven by
// the embedder's right-click submenu).  Real FFT-driven spectrum is
// M11+ work; today bar heights are pseudo-random per band but scaled
// by Host::audioLevel() so the visualisation bounces with the audio.
// No host = 0-amplitude (chrome stays visible but doesn't move).
//
// Bar colour comes from the widget's `colorband1=` attr — either a
// literal "R,G,B" triplet or a named colour resolved through the
// active gammaset (the `gammagroup=` attr).  This is what makes
// Good Ol' Winamp turn the spectrum green: DisplayVis transform
// `-4096,0,-4096` zeroes R and B but preserves G.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class VisWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace WasabiQt
