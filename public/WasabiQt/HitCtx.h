#pragma once
//
// HitCtx — per-hit-test context threaded through every Widget::hitTest
// call.  Promoted to a public header so Widget subclasses can
// participate in hit-testing without each duplicating the recursion.
//
// The existing two-walker situation (Layout::hitTestRec +
// SkinQuickItem::alphaHitListRec) collapses to a single
// virtual dispatch through Widget::hitTest, with this struct
// carrying the inputs both walkers needed.
//

#include <QImage>
#include <QSize>
#include <QString>
#include <functional>

namespace WasabiQt {

// Resolve a bitmap-id to its pixel dimensions — used by widgets that
// don't carry explicit w/h attrs and instead size to the bitmap.
// Returns QSize() when the bitmap is unknown; callers fall back to
// zero-size (skip the hit).
using ImageSizeFn = std::function<QSize(const QString &bitmapId)>;

struct HitCtx {
    // When set, only widgets with an `action=` attribute claim hits.
    // Used by classic context-menu builds; alpha-aware hit-test path
    // leaves it false.
    bool      actionOnly = false;

    // Optional bitmap-size resolver — when null, widgets that size off
    // a bitmap can't claim a hit (matches the old hitTest behaviour
    // before the alpha-aware path).
    ImageSizeFn imageSize;

    // Optional alpha buffer captured during the most recent paint.
    // When present, widgets sample painted alpha at the hit point to
    // reject clicks on visually-transparent pixels.  When null, the
    // hit-test falls back to bbox-only matching.
    const QImage *alphaBuf = nullptr;
};

}  // namespace WasabiQt
