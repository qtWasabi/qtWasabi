#pragma once
//
// <TabSheet> — tabbed pane container (used by Bento for the media-
// library/player tabs).  Real Wasabi delegates tab rendering to
// child widgets (one group per tab) and switches their visibility
// based on the active tab index.  We paint a frame around the
// content area + a tab-strip header so the tabbed structure has a
// visible anchor even before tab-switching is wired.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class TabSheetWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace WasabiQt
