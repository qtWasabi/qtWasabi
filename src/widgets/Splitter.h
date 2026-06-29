#pragma once
//
// <Splitter> — draggable divider between two panes.  Real Wasabi
// tracks the drag + repositions the adjacent panes; we paint a
// 1-pixel divider line so the visual border between panes is
// visible.  Drag tracking is a follow-up.
//
// Orientation defaults to vertical (a horizontal pane divider).
// The skin's `orientation=` attr can switch to horizontal.
//

#include <qtWasabi/Widget.h>

namespace qtWasabi {

class SplitterWidget : public Widget {
public:
    bool isInteractive() const override { return true; }
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace qtWasabi
