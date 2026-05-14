#pragma once
//
// <ScrollBar> — 3-piece track (top/middle/bottom) + thumb.  Wasabi
// reads viewport / content / position from a host-supplied list +
// draws the thumb at the proportional y.  Until host-bound lists
// are wired, paint a placeholder track + a 1/3-height thumb at the
// top so the chrome is visibly present.
//
// Orientation defaults to vertical (`orientation=` can switch).
// Drag tracking + page-up/page-down clicks are follow-ups.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class ScrollBarWidget : public Widget {
public:
    bool isInteractive() const override { return true; }
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace WasabiQt
