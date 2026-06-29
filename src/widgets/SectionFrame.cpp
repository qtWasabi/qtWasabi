// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "SectionFrame.h"

#include <qtWasabi/ColorRegistry.h>
#include <qtWasabi/GammasetRegistry.h>
#include <qtWasabi/PaintCtx.h>

#include <QColor>
#include <QPainter>
#include <QRect>

namespace qtWasabi {

namespace {

// Pull a theme colour from the bound ColorRegistry, falling back to
// the supplied literal RGB.  Same pattern MediaLibraryPanel uses for
// `color.ml.list.*`; SectionFrame resolves the `color.ml.frame.*`
// family so a host skin can retint the chrome bevel.
QColor themed(PaintCtx &ctx, const char *id, QColor fallback) {
    if (!ctx.colors) return fallback;
    return ctx.colors->resolve(QString::fromLatin1(id),
                                ctx.gammasets, fallback);
}

}  // anonymous

void SectionFrameWidget::paint(QPainter *p, PaintCtx &ctx,
                                 const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    if (attrs.value(QStringLiteral("alpha")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;

    const QColor light = themed(ctx, "color.ml.frame.light",
                                 QColor(80, 84, 92));
    const QColor dark  = themed(ctx, "color.ml.frame.dark",
                                 QColor(0, 0, 0));

    // 1-px bevel.  Matches the bevel paint in MediaLibraryPanel
    // (`paintChromeBevel`) so visual artefacts at the seam between
    // a SectionFrame-wrapped ML pane and the surrounding hand-paint
    // line up exactly.
    p->save();
    p->setPen(light);
    p->drawLine(r.topLeft(),    r.topRight());
    p->drawLine(r.topLeft(),    r.bottomLeft());
    p->setPen(dark);
    p->drawLine(r.bottomLeft() + QPoint(1, 0), r.bottomRight());
    p->drawLine(r.topRight(),                   r.bottomRight());
    p->restore();

    // Recurse children inside the inner content rect (1-px inset
    // on each side for the bevel itself).  Translate the painter
    // so children paint at (0,0)-relative coords.
    if (!children.empty()) {
        const QRect inner = r.adjusted(1, 1, -1, -1);
        if (inner.width() <= 0 || inner.height() <= 0) return;
        p->save();
        p->translate(inner.x(), inner.y());
        const QSize childCanvas(inner.width(), inner.height());
        for (const auto &c : children) {
            if (c) c->paint(p, ctx, childCanvas);
        }
        p->restore();
    }
}

}  // namespace qtWasabi
