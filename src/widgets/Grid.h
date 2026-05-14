#pragma once
//
// <Grid> — 3-slice horizontally-stretchable bitmap chrome.  Used by
// the drawer's tab buttons (Equalizer / Options / Color Themes) and
// other Wasabi UI elements.  `left=` and `right=` are fixed-width
// endcaps; `middle=` tiles between them to fill the gap.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class GridWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace WasabiQt
