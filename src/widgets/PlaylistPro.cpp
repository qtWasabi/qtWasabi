// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "PlaylistPro.h"

#include <WasabiQt/PaintCtx.h>

#include <QColor>
#include <QFont>
#include <QPainter>

namespace WasabiQt {

namespace {
void paintHostBoundPlaceholder(QPainter *p, const Widget &w,
                               const QSize &canvas,
                               const QString &label) {
    if (w.attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = w.resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;
    p->save();
    p->fillRect(r, QColor(28, 36, 50));
    p->setPen(QColor(80, 100, 130));
    p->drawRect(r.adjusted(0, 0, -1, -1));
    if (r.width() > 60 && r.height() > 18) {
        QFont f(QStringLiteral("sans-serif"));
        f.setPixelSize(10);
        p->setFont(f);
        p->setPen(QColor(180, 200, 230));
        p->drawText(r, Qt::AlignCenter, label);
    }
    p->restore();
}
}  // namespace

void PlaylistProWidget::paint(QPainter *p, PaintCtx &, const QSize &canvas) {
    paintHostBoundPlaceholder(p, *this, canvas,
                               QStringLiteral("Playlist"));
}

void PlaylistDirectoryWidget::paint(QPainter *p, PaintCtx &,
                                      const QSize &canvas) {
    paintHostBoundPlaceholder(p, *this, canvas,
                               QStringLiteral("Library"));
}

}  // namespace WasabiQt
