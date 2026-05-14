// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "ColorThemesList.h"

#include <WasabiQt/GammasetRegistry.h>
#include <WasabiQt/PaintCtx.h>

#include <QFont>
#include <QPainter>

#include <algorithm>

namespace WasabiQt {

void ColorThemesListWidget::paint(QPainter *p, PaintCtx &ctx,
                                    const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    if (!ctx.gammasets) return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;
    // Reserve the right ~14 px for the scrollbar chrome
    // (wasabi.scrollbar.vertical.*) sitting on top of the list's
    // right edge.  Otherwise the highlight bar would bleed into the
    // scroll thumb.
    const int scrollbarW = 14;
    const QRect listRect(r.x(), r.y(),
                          qMax(0, r.width() - scrollbarW),
                          r.height());
    if (ctx.colorthemesBboxOut) {
        const QPoint topLeft = p->transform().map(listRect.topLeft());
        *ctx.colorthemesBboxOut = QRect(topLeft, listRect.size());
    }
    QStringList names = ctx.gammasets->names();
    std::sort(names.begin(), names.end(),
              [](const QString &a, const QString &b){
                  return a.compare(b, Qt::CaseInsensitive) < 0;
              });
    const int rowH = 10;
    int sel = ctx.colorthemesSelected;
    if (sel < 0 || sel >= names.size()) {
        sel = 0;
        const auto *act = ctx.gammasets->active();
        if (act) {
            for (int i = 0; i < names.size(); ++i)
                if (names[i] == act->name) { sel = i; break; }
        }
    }
    const int maxRows = qMax(0, (listRect.height() - 4) / rowH);
    // `topRow` is the index of the first visible name.  Clamp to the
    // valid range, but DON'T auto-pull the selection into view —
    // doing that would prevent the user from scrolling past their
    // last-clicked row, since every paint would snap topRow back to
    // align with `sel`.  The embedder can choose to auto-scroll on
    // selection-change separately.
    int topRow = ctx.colorthemesTopRow;
    const int maxTop = qMax(0, names.size() - maxRows);
    if (topRow < 0) topRow = 0;
    if (topRow > maxTop) topRow = maxTop;
    if (ctx.colorthemesTopRowOut)
        *ctx.colorthemesTopRowOut = topRow;
    QFont qf(QStringLiteral("sans-serif"));
    qf.setPixelSize(8);
    p->save();
    p->setFont(qf);
    const int yStart = listRect.y() + 2;
    for (int i = 0; i < maxRows && (topRow + i) < names.size(); ++i) {
        const int nameIdx = topRow + i;
        const QRect row(listRect.x() + 2, yStart + i * rowH,
                        listRect.width() - 4, rowH);
        if (nameIdx == sel) {
            p->fillRect(row, QColor(80, 110, 175, 200));
            p->setPen(QColor(255, 255, 255));
        } else {
            p->setPen(QColor(220, 225, 235));
        }
        p->drawText(row.adjusted(3, 0, -3, 0),
                    Qt::AlignVCenter | Qt::AlignLeft,
                    names[nameIdx]);
    }
    p->restore();
}

}  // namespace WasabiQt
