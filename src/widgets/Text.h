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

#include <qtWasabi/Widget.h>

namespace qtWasabi {

class TextWidget : public Widget {
public:
    ~TextWidget() override;
    void onAttrsInitialized() override;
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;

protected:
    // Shrink a ticker's clip so it never paints over a sibling
    // `display="time"` widget it overlaps (keeps a scrolling song title
    // off the elapsed-time readout).  Returns `r` unchanged when there is
    // no overlapping time sibling.
    QRect keepTickerOffTime(QRect r, const QSize &canvas);

    // Auto-binding for the canonical Wasabi "Display" pattern:
    // when our id ends in `Display` (case-insensitive) AND
    // Layout::wireSteppers has tagged us with a `_stepper_key`
    // (cfgattrib whose value we should mirror), subscribe to the
    // CfgAttribStore and rewrite the displayed text on each value
    // change.  Skin-agnostic; the same pattern works for any skin
    // that ships a `*Display` text near a cfgattrib slider.
    QString m_stepperKey;
    int     m_stepperSubHandle = 0;

    // Tab state subscription — mirrors GridWidget's pattern.  Set
    // by `Layout::wireTabs` on text widgets nested inside a
    // `switch.X` group's `*.normal`/`*.active`/`*.footer` subtree.
    QString m_tabStateKey;
    int     m_tabStateValue     = -1;
    QString m_tabShowWhen;
    int     m_tabStateSubHandle = 0;
};

class SongTickerWidget : public TextWidget {};

}  // namespace qtWasabi
