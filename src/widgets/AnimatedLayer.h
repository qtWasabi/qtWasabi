#pragma once
//
// <animatedlayer> — Wasabi's sprite-strip animated layer.  Renders a
// single static frame (the `start=` frame of the sprite strip, or the
// whole `image=` when no strip dimensions are given) the same way a
// plain <layer> would.  Most chrome usages of AnimatedLayer in Modern
// PP use it as a static layer with a few-frame "breathing" effect
// that's invisible when missing.
//
// Recurses into children so an AnimatedLayer that wraps a group still
// paints the group's content.
//

#include <qtWasabi/Widget.h>

namespace qtWasabi {

class AnimatedLayerWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace qtWasabi
