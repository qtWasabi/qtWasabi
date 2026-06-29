// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Slider.h"

#include <qtWasabi/BitmapRegistry.h>
#include <qtWasabi/Host.h>
#include <qtWasabi/LayerPainter.h>
#include <qtWasabi/PaintCtx.h>

#include <QImage>
#include <QPainter>

namespace qtWasabi {

// Wasabi accepts both the short "v" and the full "vertical" spelling
// for a slider's orientation (skinparse.cpp getOrientation); anything
// else is horizontal.  Honour both so a vertical EQ band / volume
// slider written with orientation="v" doesn't drag along the wrong axis.
static bool isVerticalOrientation(const QString &o) {
    return o.compare(QLatin1String("vertical"), Qt::CaseInsensitive) == 0 ||
           o.compare(QLatin1String("v"), Qt::CaseInsensitive) == 0;
}

// An EqBand slider selects which band it drives via param= OR the older
// band= spelling (both map to the same SETPARAM action in Wasabi).  The
// host dispatches EQ_BAND by this value, so honour either.
static QString sliderParam(const QHash<QString, QString> &a) {
    const QString p = a.value(QStringLiteral("param"));
    return p.isEmpty() ? a.value(QStringLiteral("band")) : p;
}

double SliderWidget::readPosition(PaintCtx &ctx) const {
    // While the user is actively dragging, the thumb follows the drag —
    // m_localPos holds the live drag value.
    if (m_dragging && m_localPos >= 0.0) return m_localPos;
    // Otherwise prefer the host's live value for any action it tracks.
    // This is what keeps the seek thumb ADVANCING with playback (SEEK =
    // position/duration) instead of freezing at the last dragged spot,
    // and lets VOLUME / EQ_BAND reflect external changes (e.g. a Reset
    // EQ button).  writePosition already pushed the user's drag into the
    // host, so the value read back is the same one they set — now kept
    // live by playback.
    if (ctx.host) {
        const double live = ctx.host->sliderPosition(
            attrs.value(QStringLiteral("action")),
            sliderParam(attrs));
        if (qEnvironmentVariableIntValue("WASABIQT_TRACE_SLIDER") == 1)
            fprintf(stderr, "[slider] %s pos=%.4f (drag=%d localPos=%.3f)\n",
                    attrs.value(QStringLiteral("action"))
                        .toLocal8Bit().constData(),
                    live, int(m_dragging), m_localPos);
        if (live >= 0.0) return live;
    }
    // Host doesn't track this action (custom slider) — fall back to the
    // last value the user set, or a sensible centre default.
    if (m_localPos >= 0.0) return m_localPos;
    return 0.5;
}

void SliderWidget::writePosition(double pos, PaintCtx &ctx) {
    pos = qBound(0.0, pos, 1.0);
    m_localPos = pos;
    if (ctx.host) {
        // Pass `param=` so the host can dispatch by band for EQ_BAND
        // (param="1".."10" or "preamp") and any other slider action
        // that needs per-instance routing.
        ctx.host->setSliderPosition(
            attrs.value(QStringLiteral("action")), pos,
            sliderParam(attrs));
    }
    requestRepaint();
}

void SliderWidget::paint(QPainter *p, PaintCtx &ctx,
                          const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;
    // Optional background bitmap (most Modern-skin sliders paint
    // their groove via separate <layer> siblings; some do carry an
    // `image=` attr though).
    const QString bgId = attrs.value(QStringLiteral("image"));
    if (!bgId.isEmpty()) {
        QHash<QString, QString> bg = attrs;
        bg.remove(QStringLiteral("thumb"));
        LayerPainter::paintLayer(p, *ctx.bmp, bg, canvas, ctx.windowActive);
    }
    // Optional 3-slice groove track: barleft/barright caps with a
    // barmiddle slice stretched between them, laid out along the slider's
    // major axis.  Some sliders draw their track this way instead of a
    // single image= background.  No-ops when barmiddle isn't declared.
    {
        const QImage barM =
            ctx.bmp->imageFor(attrs.value(QStringLiteral("barmiddle")));
        if (!barM.isNull()) {
            const QImage barL =
                ctx.bmp->imageFor(attrs.value(QStringLiteral("barleft")));
            const QImage barR =
                ctx.bmp->imageFor(attrs.value(QStringLiteral("barright")));
            auto centred = [](int outer, int inner, int origin) {
                return origin + (outer - inner) / 2;
            };
            if (isVerticalOrientation(
                    attrs.value(QStringLiteral("orientation")))) {
                int top = r.y(), bot = r.y() + r.height();
                if (!barL.isNull()) {
                    p->drawImage(QPoint(centred(r.width(), barL.width(),
                                                r.x()), top), barL);
                    top += barL.height();
                }
                if (!barR.isNull()) {
                    bot -= barR.height();
                    p->drawImage(QPoint(centred(r.width(), barR.width(),
                                                r.x()), bot), barR);
                }
                if (bot > top)
                    p->drawImage(QRect(centred(r.width(), barM.width(),
                                               r.x()), top, barM.width(),
                                       bot - top), barM);
            } else {
                int left = r.x(), right = r.x() + r.width();
                if (!barL.isNull()) {
                    p->drawImage(QPoint(left, centred(r.height(),
                                        barL.height(), r.y())), barL);
                    left += barL.width();
                }
                if (!barR.isNull()) {
                    right -= barR.width();
                    p->drawImage(QPoint(right, centred(r.height(),
                                        barR.height(), r.y())), barR);
                }
                if (right > left)
                    p->drawImage(QRect(left, centred(r.height(),
                                       barM.height(), r.y()),
                                       right - left, barM.height()), barM);
            }
        }
    }
    // Thumb id picks down > hover > base.  Skins ship per-state
    // variants so the thumb visibly reacts to the user; the SliderWidget
    // is the only widget where `downThumb`/`hoverThumb` exist instead
    // of `downImage`/`hoverImage`.
    // Thumb precedence: down (dragging) > hover > base.
    QString thumbId = attrs.value(QStringLiteral("thumb"));
    if (m_hover && !m_dragging &&
        attrs.contains(QStringLiteral("hoverthumb")))
        thumbId = attrs.value(QStringLiteral("hoverthumb"));
    if (m_dragging && attrs.contains(QStringLiteral("downthumb")))
        thumbId = attrs.value(QStringLiteral("downthumb"));
    if (thumbId.isEmpty()) return;
    const double pos = readPosition(ctx);
    QImage thumb = ctx.bmp->imageFor(thumbId);
    if (thumb.isNull()) return;
    const bool vertical = isVerticalOrientation(
        attrs.value(QStringLiteral("orientation")));
    int thumbX, thumbY;
    if (vertical) {
        // Vertical sliders run top-to-bottom: pos=0 at top, pos=1 at
        // bottom.  EQ_BAND convention is pos=0.5 = 0 dB at the centre.
        const int travel = qMax(0, r.height() - thumb.height());
        thumbX = r.x() + (r.width() - thumb.width()) / 2;
        thumbY = r.y() + int(pos * travel);
    } else {
        const int travel = qMax(0, r.width() - thumb.width());
        thumbX = r.x() + int(pos * travel);
        thumbY = r.y() + (r.height() - thumb.height()) / 2;
    }
    p->drawImage(thumbX, thumbY, thumb);
}

namespace {
// Compute normalised pos [0..1] from a click point relative to the
// slider's resolved rect.  Click maps to the *centre* of the thumb,
// so we subtract half the thumb dimension from both the click and
// the travel range — matches Wasabi's expectation that dragging puts
// the thumb's centre under the cursor rather than its top-left.
double pointToPos(QPoint p, const QRect &r, const QSize &thumb,
                   bool vertical) {
    if (vertical) {
        const int travel = qMax(1, r.height() - thumb.height());
        const int half   = thumb.height() / 2;
        const int y      = qBound(0, p.y() - r.y() - half, travel);
        return double(y) / double(travel);
    }
    const int travel = qMax(1, r.width() - thumb.width());
    const int half   = thumb.width() / 2;
    const int x      = qBound(0, p.x() - r.x() - half, travel);
    return double(x) / double(travel);
}
}  // namespace

namespace {
QSize thumbSize(const QHash<QString, QString> &attrs,
                 BitmapRegistry *bmp) {
    if (!bmp) return QSize(8, 8);
    const QString id = attrs.value(QStringLiteral("thumb"));
    const QImage im = bmp->imageFor(id);
    return im.isNull() ? QSize(8, 8) : im.size();
}
}  // namespace

void SliderWidget::onLeftButtonDown(QPoint p, PaintCtx &ctx) {
    m_dragging = true;
    // Click point arrives in CANVAS coords.  The widget's own canvas-
    // space rect is whatever the hit-test most recently resolved into
    // `lastCanvasRect` — that's also where the thumb is painted, so
    // pos↔pixel arithmetic works against the same reference frame.
    const QRect r = lastCanvasRect.isValid()
        ? lastCanvasRect
        : resolveRectFromAttrs(attrs, QSize(0, 0));
    const QSize thumb = thumbSize(attrs, ctx.bmp);
    const bool vertical = isVerticalOrientation(
        attrs.value(QStringLiteral("orientation")));
    writePosition(pointToPos(p, r, thumb, vertical), ctx);
}

void SliderWidget::onMouseMove(QPoint p, PaintCtx &ctx) {
    if (!m_dragging) {
        // Hovering (not dragging): light the hover thumb.
        if (!m_hover) { m_hover = true; requestRepaint(); }
        return;
    }
    const QRect r = lastCanvasRect.isValid()
        ? lastCanvasRect
        : resolveRectFromAttrs(attrs, QSize(0, 0));
    const QSize thumb = thumbSize(attrs, ctx.bmp);
    const bool vertical = isVerticalOrientation(
        attrs.value(QStringLiteral("orientation")));
    writePosition(pointToPos(p, r, thumb, vertical), ctx);
}

void SliderWidget::onMouseLeave(PaintCtx &) {
    if (m_hover) { m_hover = false; requestRepaint(); }
}

void SliderWidget::onLeftButtonUp(QPoint, PaintCtx &) {
    if (m_dragging) {
        m_dragging = false;
        requestRepaint();
    }
}

}  // namespace qtWasabi
