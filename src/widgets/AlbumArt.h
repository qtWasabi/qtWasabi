#pragma once
//
// <albumart> — renders the embedder-supplied album cover scaled to
// the widget's bbox.  Falls back to a single-colour placeholder tile
// when no Host or no track is loaded.  Skin scripts that want a
// default cover can place a <layer image="..."/> behind the
// <AlbumArt> as a sibling, which still paints normally.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class AlbumArtWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace WasabiQt
