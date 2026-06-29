// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <qtWasabi/MilkdropWidget.h>

#include <qtWasabi/PaintCtx.h>

#include <QDateTime>
#include <QPainter>
#include <QTransform>

namespace qtWasabi {

void MilkdropWidget::paint(QPainter *p, PaintCtx &, const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    // No drawing — the embedder's GL overlay paints into this rect.
    // Bump the liveness timestamp AND record the canvas-space rect
    // so the embedder's overlay tracker knows both that we're being
    // painted and *where* on the canvas to position the GL surface.
    lastPaintedAtMs = QDateTime::currentMSecsSinceEpoch();

    // Compute canvas-space rect from QPainter's current transform.
    // resolveRect(canvas) gives the widget's local rect (already
    // honoring relat / fitparent semantics).  TreePainter chains
    // `translate(rect.x, rect.y)` for each container, so the
    // accumulated transform maps the widget's local origin (0,0) to
    // its absolute canvas coords.  Cheaper and more reliable than
    // re-running the layout walker.
    const QRect localR = resolveRect(canvas);
    if (p) {
        const QTransform t = p->transform();
        const QPointF topLeft = t.map(QPointF(0, 0));
        lastCanvasRect = QRect(int(topLeft.x()), int(topLeft.y()),
                                localR.width(), localR.height());
    } else {
        lastCanvasRect = localR;
    }
}

}  // namespace qtWasabi
