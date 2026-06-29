#pragma once
//
// <LayoutStatus> — chrome strip that hosts the layout-switch
// buttons (the small icons that toggle between shade/normal/etc.
// modes in Wasabi's frame).  Real Wasabi paints the host's
// LayoutSwitchTable bitmaps; we render a flat-coloured strip
// matching the surrounding chrome until host data wires in.
//

#include <qtWasabi/Widget.h>

namespace qtWasabi {

class LayoutStatusWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace qtWasabi
