#pragma once
//
// <rect> — solid filled or 1-px-outlined coloured rectangle.  Used by
// Wasabi for video.group's black-fill background, tooltip-border
// strokes, debug overlays, and any skin that wants a flat-colour fill
// without shipping a bitmap.
//
// `color="R,G,B"` is a literal triplet; `color="someid"` is a named
// entry in the colour registry (resolved against the active gammaset).
// `filled="0"` draws a 1-px outline; otherwise the rect is filled.
//

#include <qtWasabi/Widget.h>

namespace qtWasabi {

class RectWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace qtWasabi
