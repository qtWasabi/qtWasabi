#pragma once
//
// <windowholder> / <wmh> — host slot for an embedded sub-window
// (video output, AVS frame, browser pane, …).  qtWasabi doesn't yet
// host the embedded surfaces, so this widget paints a black
// placeholder rect that visually matches what real Winamp shows
// while the embedded component is loading or unavailable.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class WindowHolderWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace WasabiQt
