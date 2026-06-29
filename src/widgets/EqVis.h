#pragma once
//
// <EqVis> — equalizer visualisation graph.  Wasabi's seqvis widget
// renders a curve / bar visualisation of the current EQ band state.
// We paint a 10-band bar graph driven by host slider positions
// (EQ_BAND_0…EQ_BAND_9 + EQ_PREAMP) when available; without host
// data, render a flat-zero baseline so the widget has visible
// geometry instead of an empty rect.
//

#include <qtWasabi/Widget.h>

namespace qtWasabi {

class EqVisWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace qtWasabi
