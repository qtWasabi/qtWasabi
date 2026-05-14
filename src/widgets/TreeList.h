#pragma once
//
// <TreeList> — hierarchical browse list (host-supplied tree).
// Renders a dark panel + label until host-bound tree rendering
// lands; bounds are visible so layout debugging works.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class TreeListWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace WasabiQt
