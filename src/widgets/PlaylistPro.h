#pragma once
//
// <PlaylistPro> — embedded playlist component used by Bento /
// modern skins to host the queued-tracks list inside the player.
// Real Wasabi delegates to a PlaylistComponent that draws rows
// from the host's playlist data source.  Until the host wires in,
// paint a flat-colour panel matching Wasabi's "no playlist
// loaded" placeholder, with the widget id as label so debugging
// can identify the bbox visually.
//
// Hit-testing reuses Widget's default — clicks land here but
// fall through to no handler until host integration arrives.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class PlaylistProWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

class PlaylistDirectoryWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace WasabiQt
