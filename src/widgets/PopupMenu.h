#pragma once
//
// <PopupMenu> — right-click context menu (Wasabi spawns a host
// QMenu / native menu).  Until a real QMenu integration lands,
// PopupMenu paints nothing — its presence is a marker that the
// skin author wants a context-menu spawn site.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class PopupMenuWidget : public Widget {
public:
    void paint(QPainter *, PaintCtx &, const QSize &) override {}
};

}  // namespace WasabiQt
