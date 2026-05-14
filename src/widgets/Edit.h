#pragma once
//
// <edit> / <wasabi.edit.box> — single-line text input.  Real text
// editing requires focus + input-method handoff, which is a separate
// milestone; for now we paint the declared `text=` / `default=`
// string inside a 1-px-outlined dark-blue rect at the widget bounds.
// This keeps every skin that ships an embedded edit field visually
// honest (the box appears where it's supposed to be) without claiming
// keyboard ownership we don't yet handle.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class EditWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace WasabiQt
