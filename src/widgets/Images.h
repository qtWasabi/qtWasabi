#pragma once
//
// <images source="volume|balance" images="<bitmap-id>"
//         imagesspacing="<stride>" w=... h=.../>
// Multi-frame bitmap strip indexed by a host-driven value.  Used by
// classic Winamp skins (DeClassified et al.) for the volume rail
// and balance rail — VOLUME.BMP / BALANCE.BMP are a vertical strip
// of 28 frames; the widget picks one based on volume (0..1) or
// balance (-1..+1) and blits it as the rail background.
//

#include <qtWasabi/Widget.h>

namespace qtWasabi {

class ImagesWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace qtWasabi
