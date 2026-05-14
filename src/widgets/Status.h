#pragma once
//
// <status playBitmap="X" pauseBitmap="Y" stopBitmap="Z"/> — classic-
// skin widget that chooses one of three bitmaps based on the host's
// playback state.  Without a Maki script driving visibility, qtWasabi
// statically picks the matching bitmap and paints it as a regular
// layer, with a left-edge alignment correction so the three bitmaps
// (which often have different internal left padding) all land at the
// same visible x as the play bitmap's leftmost lit column — that's
// what produces the uniform 3-px gap from the LED strip in every
// playback state.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class StatusWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace WasabiQt
