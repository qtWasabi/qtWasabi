#pragma once
//
// SectionFrameWidget — bevelled chrome frame around a content rect.
// The rectangular silver-bevel border that wraps each pane (sidebar
// tree, artist column, album column, track list, status bar) in the
// canonical Media Library UI.
//
// Functionally a styled Container — paints a 1-px highlight on
// top+left and a 1-px shadow on bottom+right, then recurses into
// children at the inner content rect.  Colour-resolved through
// `ColorRegistry` so a host skin can retint the chrome via
// `color.ml.frame.light` / `color.ml.frame.dark` overrides; falls
// back to a neutral grey palette for skins that don't.
//
// Designed for two consumption paths:
//   1. Direct XML use — a host skin can drop
//      `<SectionFrame x=… y=… w=… h=…>… children …</SectionFrame>`
//      anywhere a Container would go, but with the bevel pre-baked.
//   2. Programmatic use by the gen_ml host port — `MlHostWidget`
//      builds its multi-pane layout by stacking
//      SectionFrames around `TreeListWidget` / `MultiColumnList`
//      content.
//

#include <qtWasabi/Widget.h>

namespace qtWasabi {

class SectionFrameWidget : public Widget {
public:
    SectionFrameWidget()  = default;
    ~SectionFrameWidget() = default;

    bool isContainer() const override { return true; }

    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace qtWasabi
