#pragma once
//
// <hideobject> — wrapper that hides its children unconditionally.
// Real Wasabi uses this for design-time-only widgets (templates,
// placeholders) that shouldn't render at runtime.
//
// Unlike a `visible="0"` group (which still affects layout
// expansion / hit-test traversal), HideObject is a hard veto on
// rendering AND hit-testing: a click that lands in its bounds
// passes through to whatever is below.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class HideObjectWidget : public Widget {
public:
    void paint(QPainter *, PaintCtx &, const QSize &) override {}
    Widget *hitTest(QPoint, QPoint, const QSize &,
                     HitCtx &, QRect *) override { return nullptr; }
};

}  // namespace WasabiQt
