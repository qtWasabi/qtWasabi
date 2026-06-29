#pragma once
//
// <GuiList> / <List> — generic scrollable list backed by a host-
// supplied data source.  Real Wasabi renders rows + header +
// integrated scrollbar.  Until host-bound row rendering lands,
// paint a dark panel + label so the list bounds are visible.
//

#include <qtWasabi/Widget.h>

namespace qtWasabi {

class GuiListWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace qtWasabi
