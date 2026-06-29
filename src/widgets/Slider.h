#pragma once
//
// <slider> — draggable bitmap thumb with an optional groove bitmap
// and host-driven position.  Action mapping (`action=` attr) names
// the host slider to read/write — VOLUME / PAN / SEEK / EQ_BAND.
// Orientation `vertical` vs default horizontal.  For EQ_BAND-style
// sliders without host wiring, the thumb defaults to centred so the
// rails look inert-but-honest rather than missing-a-thumb.
//
// The drag-tracking state (capture point, pos at drag start) lives
// on this class; the rest of the mouse handling is centralised in
// qtamp.
//

#include <qtWasabi/Widget.h>

namespace qtWasabi {

class SliderWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
    bool isInteractive() const override { return true; }
    bool capturesMouse() const override { return true; }

    // Drag handling.  Down/move/up at canvas coords (the centralised
    // qtamp event router already translates window coords to canvas).
    // Pos is recomputed from the click point relative to the slider's
    // resolved rect — both initial down and subsequent moves while the
    // pointer stays captured.  Release ends the drag.  Sliders whose
    // action= isn't known to the host fall back to a local position
    // member so the thumb still tracks the cursor visually.
    void onLeftButtonDown(QPoint p, PaintCtx &ctx) override;
    void onLeftButtonUp  (QPoint p, PaintCtx &ctx) override;
    void onMouseMove     (QPoint p, PaintCtx &ctx) override;
    void onMouseLeave    (PaintCtx &ctx) override;

private:
    void writePosition(double pos, PaintCtx &ctx);
    double readPosition(PaintCtx &ctx) const;

    bool   m_dragging   = false;
    bool   m_hover      = false;
    // Local position fallback for actions the host doesn't accept
    // (EQ_BAND/preamp + any unwired action).  -1 means "use the host
    // value as-is".  Once the user drags the slider, this absorbs the
    // value so subsequent paints reflect the user's last drag even
    // when the host has no setter for the action.
    double m_localPos   = -1.0;
};

}  // namespace qtWasabi
