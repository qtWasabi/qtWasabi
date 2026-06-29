#pragma once
//
// <DropDownList> — combo with edit.  Wasabi paints the closed-
// state arrow button + the active selection's text.  We render a
// flat panel + a right-edge arrow placeholder so the bounding
// box is visually present.  Drop-down expansion is a follow-up.
//

#include <qtWasabi/Widget.h>

namespace qtWasabi {

class DropDownListWidget : public Widget {
public:
    bool isInteractive() const override { return true; }
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace qtWasabi
