#pragma once
//
// <ProgressGrid> — the seek-progress fill bar that grows from the
// left edge of its bounding rect as the song plays.  Skin XML
// declares it with `left=` / `middle=` (3-slice bitmaps) and a
// host-driven width; we tile `middle` from the rect's left up to
// `position / duration × rect.w`.  When the skin uses the simpler
// form with just `image=`, we use that as the middle slice.
//

#include <qtWasabi/Widget.h>

namespace qtWasabi {

class ProgressGridWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace qtWasabi
