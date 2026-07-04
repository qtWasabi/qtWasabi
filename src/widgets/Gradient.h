#pragma once
//
// <gradient> — Wasabi's procedural colour-ramp widget.  The Win9x-style
// skins paint their titlebars with it: two overlapping gradients whose
// stops are neutral grey tagged with a titlebar gammagroup, so the
// active Color Theme supplies the actual hue.
//
// Attribute contract (mirrors XuiGradientWnd + bfc/draw/gradient):
//   gradient_x1/y1/x2/y2 — gradient line endpoints as fractions of the
//                          widget box; defaults 0,0 → 1,1.
//   points               — "pos=r,g,b[,a];pos=r,g,b[,a];…", stop colours
//                          filtered through `gammagroup`.
//   mode                 — "linear" (default) or "circular".
//

#include <qtWasabi/Widget.h>

namespace qtWasabi {

class GradientWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace qtWasabi
