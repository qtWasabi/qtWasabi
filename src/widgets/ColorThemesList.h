#pragma once
//
// <ColorThemes:List> — the drawer's colour-themes picker.  XML tag
// normalises to `colorthemes_list`.  Renders the list of available
// gammasets one row per line, highlights the active (or visually-
// selected) one, and writes the on-screen bbox + scrolled-to topRow
// back through `PaintCtx::colorthemesBboxOut` /
// `colorthemesTopRowOut` so the embedder's click handler can map a
// Y-coord to a row index.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class ColorThemesListWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace WasabiQt
