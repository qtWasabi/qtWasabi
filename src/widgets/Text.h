#pragma once
//
// <text> / <songticker> — bitmap-font or TrueType text rendering.
// Both share TextPainter as the actual glyph compositor; SongTicker
// implicitly enables the scrolling ticker behaviour (a <text>
// widget can opt in explicitly via `ticker="1"`).
//
// Ticker scrolling: the text scrolls leftward when the resolved
// string is wider than the widget rect; wraps with a small gap;
// loops continuously.  Speed comes from the skin via `tickspeed=`
// / `speed=` / Modern PP's `pixelsperframe`; default 12 px/s.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class TextWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

class SongTickerWidget : public TextWidget {};

}  // namespace WasabiQt
