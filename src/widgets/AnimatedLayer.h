#pragma once
//
// <animatedlayer> — Wasabi's sprite-strip animated layer.  Until we
// wire a frame-pump timer (Phase 6), render the static `image=`
// (first frame) the same way a plain <layer> would.  Most chrome
// usages of AnimatedLayer in Modern PP use it as a static layer with
// a few-frame "breathing" effect that's invisible when missing.
//
// Recurses into children so AnimatedLayer that wrap a group still
// expose their content.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class AnimatedLayerWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace WasabiQt
