#pragma once
//
// <RadioGroup> — group of mutually-exclusive radio buttons.
// Real Wasabi delegates rendering to child widgets; we paint a
// thin outline around the group so the boundary is visible
// during layout debugging.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class RadioGroupWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace WasabiQt
