#pragma once
//
// <slider> — draggable bitmap thumb with an optional groove bitmap
// and host-driven position.  Action mapping (`action=` attr) names
// the host slider to read/write — VOLUME / PAN / SEEK / EQ_BAND.
// Orientation `vertical` vs default horizontal.  For EQ_BAND-style
// sliders without host wiring, the thumb defaults to centred so the
// rails look inert-but-honest rather than missing-a-thumb.
//
// Phase 6 will move the drag-tracking state (capture point, pos at
// drag start) onto this class; today's mouse handling is centralised
// in qtamp.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class SliderWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
    bool isInteractive() const override { return true; }
};

}  // namespace WasabiQt
