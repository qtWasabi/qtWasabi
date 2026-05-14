// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Text.h"

#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/FontRegistry.h>
#include <WasabiQt/PaintCtx.h>
#include <WasabiQt/TextPainter.h>

#include <QDateTime>
#include <QPainter>

namespace WasabiQt {

void TextWidget::paint(QPainter *p, PaintCtx &ctx,
                        const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    // The displayed string comes through the resolver as "songtitle"
    // / "songinfo" or whatever the embedder wires up.
    QHash<QString, QString> a = attrs;
    if (tag == QStringLiteral("songticker") &&
        a.value(QStringLiteral("display")).isEmpty()) {
        a.insert(QStringLiteral("display"),
                 QStringLiteral("songtitle"));
    }
    // Ticker scrolling: <songticker> is implicitly a ticker, and
    // <text ticker="1"> opts into scrolling explicitly.  The text
    // scrolls leftward when the resolved string is wider than the
    // widget rect; wraps with a small gap; loops continuously.
    const bool isTicker =
        (tag == QStringLiteral("songticker")) ||
        (a.value(QStringLiteral("ticker")) == QStringLiteral("1"));
    if (isTicker && ctx.font && ctx.bmp) {
        // Resolve display string the same way TextPainter does so we
        // can measure its bitmap-font width.
        const QString fontId = a.value(QStringLiteral("font"));
        const auto *fd = ctx.font->find(fontId);
        QString tickText;
        const QString display = a.value(QStringLiteral("display"));
        if (ctx.resolver && !display.isEmpty())
            tickText = ctx.resolver(display);
        // Mirror paintText's id-based fallback so the ticker
        // pre-check sees the same string the painter will draw.
        if (tickText.isEmpty() && ctx.resolver) {
            const QString textId = a.value(QStringLiteral("id"));
            if (!textId.isEmpty()) tickText = ctx.resolver(textId);
        }
        if (tickText.isEmpty())
            tickText = a.value(QStringLiteral("default"));
        if (tickText.isEmpty())
            tickText = a.value(QStringLiteral("text"));
        const QRect r = resolveRect(canvas);
        if (fd && fd->charWidth > 0 && r.width() > 0 &&
            !tickText.isEmpty()) {
            const int tickW =
                tickText.size() * fd->charWidth +
                qMax(0, tickText.size() - 1) * fd->hSpacing;
            if (tickW > r.width()) {
                // Scroll: 30 px/s with a `gap` of charWidth*4 before
                // the text loops back into view.
                const int gap = fd->charWidth * 4;
                const int totalW = tickW + gap;
                const qint64 ms = QDateTime::currentMSecsSinceEpoch();
                // Speed comes from the skin: in real Wasabi a Maki
                // script calls Songticker.setSpeed(N) at startup.
                // Until SkinRuntime drives that universally, the skin
                // can set it on the XML via `tickspeed=` or `speed=`,
                // and we also honour Modern PP's `pixelsperframe`
                // (Wasabi's actual attr name for songticker scroll
                // rate).  Default = 12 px/s, matching the Wasabi
                // Modern stock speed.
                auto attrInt = [&](const QString &k) {
                    return a.value(k).toInt();
                };
                int speed = attrInt(QStringLiteral("tickspeed"));
                if (speed <= 0) speed = attrInt(QStringLiteral("speed"));
                if (speed <= 0)
                    speed = attrInt(QStringLiteral("pixelsperframe"))
                            * 30;  // ~30 fps in classic Wasabi
                if (speed <= 0) speed = 12;
                const int offset =
                    int((ms * speed / 1000) % qint64(totalW));
                p->save();
                p->setClipRect(r);
                p->translate(-offset, 0);
                TextPainter::paintText(p, *ctx.font, *ctx.bmp, a, canvas,
                                        ctx.resolver, ctx.colors,
                                        ctx.gammasets,
                                        /*clipToWidget=*/false);
                p->translate(totalW, 0);
                TextPainter::paintText(p, *ctx.font, *ctx.bmp, a, canvas,
                                        ctx.resolver, ctx.colors,
                                        ctx.gammasets,
                                        /*clipToWidget=*/false);
                p->restore();
                return;
            }
        }
    }
    TextPainter::paintText(p, *ctx.font, *ctx.bmp, a, canvas,
                           ctx.resolver, ctx.colors, ctx.gammasets);
}

}  // namespace WasabiQt
