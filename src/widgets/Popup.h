#pragma once
//
// <Popup> — z-overlay surface that displays popup contents.
// Real Wasabi positions the popup over the layout, fades in,
// hosts arbitrary child widgets, and captures input until
// dismissed.  We paint a translucent dark panel so it's
// visually distinct from the underlying chrome.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class PopupWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace WasabiQt
