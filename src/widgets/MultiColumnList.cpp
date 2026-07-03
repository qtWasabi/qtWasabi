// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "MultiColumnList.h"

#include <qtWasabi/ColorRegistry.h>
#include <qtWasabi/GammasetRegistry.h>
#include <qtWasabi/PaintCtx.h>

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPolygon>

namespace qtWasabi {

namespace {

QColor themed(PaintCtx &ctx, const char *id, QColor fallback) {
    if (!ctx.colors) return fallback;
    return ctx.colors->resolve(QString::fromLatin1(id),
                                ctx.gammasets, fallback);
}

// Resolve the first defined of several candidate skin colour names —
// lets us prefer the canonical wa_dlg `wasabi.*` element names (the
// exact ones gen_ff/ff_ipc.cpp reads to build the genex) so the list
// is themed correctly for ANY skin, falling back to Bento-private
// aliases then a literal.
QColor firstThemed(PaintCtx &ctx, std::initializer_list<const char *> ids,
                    QColor fallback) {
    for (const char *id : ids) {
        QColor got = themed(ctx, id, QColor());
        if (got.isValid()) return got;
    }
    return fallback;
}

}  // anonymous

int MultiColumnListWidget::appendColumn(const QString &label, int width, int align) {
    MclColumn c;
    c.label = label;
    c.width = width;
    c.align = align;
    m_columns.append(c);
    return m_columns.size() - 1;
}

void MultiColumnListWidget::clearColumns() {
    m_columns.clear();
}

void MultiColumnListWidget::appendRow(const QStringList &cells) {
    m_rows.append(cells);
}

void MultiColumnListWidget::clearRows() {
    m_rows.clear();
    m_selection = -1;
}

void MultiColumnListWidget::setSelection(int row) {
    m_selection = row;
    requestRepaint();
}

void MultiColumnListWidget::paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    if (attrs.value(QStringLiteral("alpha")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;
    m_lastRect = QRect(p->transform().map(r.topLeft()), r.size());

    // Canonical wa_dlg colour slots (the exact wasabi.* element names
    // gen_ff/ff_ipc.cpp reads) → correct for every skin.
    const QColor bg        = firstThemed(ctx,
        {"wasabi.list.background", "color.display.bg"}, QColor(0, 0, 0));
    const QColor headerBg  = firstThemed(ctx,
        {"wasabi.list.column.background"}, QColor(20, 22, 26));
    const QColor headerTxt = firstThemed(ctx,
        {"wasabi.list.column.text", "wasabi.window.text"}, QColor(210, 210, 210));
    // Listview-header raised bevel: top/middle/bottom frame colours.
    const QColor frameTop  = firstThemed(ctx,
        {"wasabi.list.column.frame.top"}, headerBg.lighter(135));
    const QColor frameBot  = firstThemed(ctx,
        {"wasabi.list.column.frame.bottom"}, headerBg.darker(140));
    const QColor headerSep = firstThemed(ctx,
        {"wasabi.list.column.frame.middle", "wasabi.border.sunken"},
        QColor(48, 52, 60));
    const QColor text      = firstThemed(ctx,
        {"wasabi.list.text", "color.display"}, QColor(220, 225, 235));
    const QColor selBg     = firstThemed(ctx,
        {"wasabi.list.text.selected.background", "wasabi.list.item.selected",
         "color.selected.active.bg"}, QColor(58, 80, 140));
    const QColor selFg     = firstThemed(ctx,
        {"wasabi.list.text.selected", "color.selected.active"},
        QColor(255, 255, 255));
    // Alternating-row stripe (WADLG_ITEMBG2): a subtle shade off `bg`.
    const QColor bg2       = firstThemed(ctx,
        {"wasabi.list.background.alt"},
        bg.lightnessF() < 0.5 ? bg.lighter(160) : bg.darker(108));
    // Unfocused selection (WADLG_INACT_SELBAR): dimmed selection bar.
    const QColor inactSelBg = firstThemed(ctx,
        {"wasabi.list.text.selected.background.inactive"}, selBg.darker(155));

    p->save();
    p->fillRect(r, bg);

    QFont rowFont(QStringLiteral("Tahoma"));
    rowFont.setPixelSize(11);
    const QFontMetrics rowFm(rowFont);
    const int rowH    = qMax(14, rowFm.height() + 2);
    const int headerH = rowH + 2;
    m_lastHeaderH     = headerH;
    m_lastRowH        = rowH;
    // Hit-test geometry is compared against canvas-space click coords, so
    // anchor it to the transform-mapped rect top (m_lastRect), NOT the
    // local r.y() — the holder paints the panes through a translated
    // painter, so r.y() is offset from the canvas y the clicks arrive in.
    m_lastFirstRowY   = m_lastRect.y() + headerH + 1;

    // Header strip.  One band across the top of the rect, column
    // labels separated by a 1-px vertical divider, optional sort
    // indicator triangle at the right edge of each cell.
    const QRect header(r.x(), r.y(), r.width(), headerH);
    p->fillRect(header, headerBg);
    // Raised listview-header bevel: light top edge, dark bottom edge
    // (WADLG_LISTHEADER_FRAME_TOP/BOTTOM), the classic Winamp column
    // header look.
    p->setPen(frameTop);
    p->drawLine(header.topLeft(), header.topRight());
    p->setPen(frameBot);
    p->drawLine(header.bottomLeft(), header.bottomRight());
    p->setFont(rowFont);
    p->setPen(headerTxt);
    int xcol = header.x() + 6;
    for (int i = 0; i < m_columns.size(); ++i) {
        const MclColumn &c = m_columns[i];
        const int cw = qMax(20, c.width);
        const QRect cellR(xcol, header.y(), cw - 12, header.height());
        int flag = Qt::AlignVCenter;
        switch (c.align) {
            case 1: flag |= Qt::AlignHCenter; break;
            case 2: flag |= Qt::AlignRight;   break;
            default: flag |= Qt::AlignLeft;
        }
        p->drawText(cellR, flag, c.label);
        // Sort indicator.
        if (c.sortMode != 0) {
            const int tx = cellR.right() - 8;
            const int ty = cellR.y() + (cellR.height() - 6) / 2;
            QPolygon tri;
            if (c.sortMode > 0) {
                tri << QPoint(tx,     ty + 5)
                    << QPoint(tx + 6, ty + 5)
                    << QPoint(tx + 3, ty);
            } else {
                tri << QPoint(tx,     ty)
                    << QPoint(tx + 6, ty)
                    << QPoint(tx + 3, ty + 5);
            }
            p->setBrush(text);
            p->setPen(Qt::NoPen);
            p->drawPolygon(tri);
            p->setPen(text);
        }
        // Column separator (skip after last).
        if (i + 1 < m_columns.size()) {
            p->setPen(headerSep);
            p->drawLine(xcol + cw - 6, header.y() + 2,
                         xcol + cw - 6, header.bottom() - 2);
            p->setPen(text);
        }
        xcol += cw;
    }

    // Rows.
    int y = r.y() + headerH + 1;
    for (int row = 0; row < m_rows.size(); ++row) {
        if (y + rowH > r.bottom()) break;
        const QStringList &cells = m_rows[row];
        const QRect rowR(r.x(), y, r.width(), rowH);
        const bool selected = (row == m_selection);
        if (selected)
            p->fillRect(rowR, m_active ? selBg : inactSelBg);
        else if (row & 1)
            p->fillRect(rowR, bg2);            // alternating stripe
        int xcell = rowR.x() + 6;
        p->setPen(selected ? (m_active ? selFg : text) : text);
        for (int c = 0; c < m_columns.size(); ++c) {
            const int cw = qMax(20, m_columns[c].width);
            const QString s = c < cells.size() ? cells[c] : QString();
            int flag = Qt::AlignVCenter;
            switch (m_columns[c].align) {
                case 1: flag |= Qt::AlignHCenter; break;
                case 2: flag |= Qt::AlignRight;   break;
                default: flag |= Qt::AlignLeft;
            }
            p->drawText(QRect(xcell, rowR.y(), cw - 12, rowH),
                        flag, s);
            xcell += cw;
        }
        y += rowH;
    }
    p->restore();
}

void MultiColumnListWidget::onLeftButtonDown(QPoint pos, PaintCtx &) {
    if (m_lastRect.isEmpty()) return;
    if (!m_lastRect.contains(pos)) return;

    // Header click — toggle sort indicator on the clicked column.
    if (pos.y() < m_lastFirstRowY) {
        int xcol = m_lastRect.x() + 6;
        for (int i = 0; i < m_columns.size(); ++i) {
            const int cw = qMax(20, m_columns[i].width);
            if (pos.x() >= xcol - 6 && pos.x() < xcol + cw - 6) {
                // Cycle: 0 → 1 (asc) → -1 (desc) → 0
                MclColumn &c = m_columns[i];
                if (c.sortMode == 0) c.sortMode = 1;
                else if (c.sortMode == 1) c.sortMode = -1;
                else c.sortMode = 0;
                // Reset other columns' sort indicators (single-
                // column sort is the standard pattern).
                for (int j = 0; j < m_columns.size(); ++j)
                    if (j != i) m_columns[j].sortMode = 0;
                requestRepaint();
                return;
            }
            xcol += cw;
        }
        return;
    }

    // Body click — select row.
    if (m_lastRowH <= 0) return;
    const int relY = pos.y() - m_lastFirstRowY;
    const int row  = relY / m_lastRowH;
    if (row >= 0 && row < m_rows.size()) {
        m_selection = row;
        requestRepaint();
    }
}

}  // namespace qtWasabi
