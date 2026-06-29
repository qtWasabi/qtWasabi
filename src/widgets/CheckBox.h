#pragma once
//
// <CheckBox> — XUI checkbox with text label.  Wasabi paints a
// 13×13 bitmap (checked/unchecked variants) followed by the
// `text=` label.  Until skin-supplied bitmaps are wired, we paint
// a deliberately-simple 11×11 frame + check mark with the text
// label to the right, so config dialogs that ship CheckBox
// widgets have visible content instead of an empty rect.
//
// Click toggles the checked state.  No host-binding yet — the
// state lives on the widget instance.
//

#include <qtWasabi/Widget.h>

namespace qtWasabi {

class CheckBoxWidget : public Widget {
public:
    bool isInteractive() const override { return true; }
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
    void onLeftButtonDown(QPoint p, PaintCtx &ctx) override;

private:
    bool m_checked = false;
};

}  // namespace qtWasabi
