// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "PlaylistPro.h"

#include <qtWasabi/Host.h>
#include <qtWasabi/PaintCtx.h>

#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>

namespace qtWasabi {

namespace {

QString formatDuration(qint64 ms) {
    if (ms <= 0) return QString();
    const qint64 totalSec = ms / 1000;
    return QString::asprintf("%lld:%02lld",
                              static_cast<long long>(totalSec / 60),
                              static_cast<long long>(totalSec % 60));
}

// Shared row renderer.  Caller passes the bbox already clipped /
// inset; we paint into it.  Returns the row height we used so the
// click handler can map y-coords back to row indices.
int paintRowsCommon(QPainter *p, const QRect &rect,
                    int rowCount,
                    int currentRow,
                    int &topRow,
                    const std::function<QString(int)> &textOf,
                    const std::function<QString(int)> &rightTextOf,
                    const std::function<QColor(int)> &fgOf,
                    const QColor &bgColor,
                    const QColor &accentColor) {
    if (rect.width() <= 0 || rect.height() <= 0) return 12;
    p->save();
    p->fillRect(rect, bgColor);
    p->setPen(QColor(60, 80, 105));
    p->drawRect(rect.adjusted(0, 0, -1, -1));

    QFont qf(QStringLiteral("sans-serif"));
    qf.setPixelSize(10);
    p->setFont(qf);
    const QFontMetrics fm(qf);
    const int rowH = qMax(11, fm.height() + 1);

    const QRect inner = rect.adjusted(3, 2, -3, -2);
    const int maxRows = qMax(0, inner.height() / rowH);
    const int maxTop  = qMax(0, rowCount - maxRows);
    if (topRow < 0) topRow = 0;
    if (topRow > maxTop) topRow = maxTop;

    p->setClipRect(inner);
    for (int i = 0; i < maxRows && (topRow + i) < rowCount; ++i) {
        const int idx = topRow + i;
        const QRect row(inner.x(), inner.y() + i * rowH,
                         inner.width(), rowH);
        if (idx == currentRow) {
            p->fillRect(row, accentColor);
        }
        const QString right = rightTextOf ? rightTextOf(idx) : QString();
        int rightW = right.isEmpty() ? 0 :
            fm.horizontalAdvance(right) + 6;
        p->setPen(fgOf ? fgOf(idx) : QColor(220, 225, 235));
        const QRect textRect(row.x() + 2, row.y(),
                              qMax(0, row.width() - rightW - 4),
                              row.height());
        p->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                    fm.elidedText(textOf(idx), Qt::ElideRight,
                                  textRect.width()));
        if (!right.isEmpty()) {
            const QRect rRect(row.right() - rightW - 2, row.y(),
                              rightW, row.height());
            p->drawText(rRect, Qt::AlignVCenter | Qt::AlignRight, right);
        }
    }
    p->restore();
    return rowH;
}

}  // namespace

void paintPlaylistRows(QPainter *p, PaintCtx &ctx,
                       const QRect &rect, int &topRow) {
    Host *host = ctx.host;
    const int rowCount = host ? host->playlistRowCount() : 0;
    const int curRow   = host ? host->playlistCurrentRow() : -1;
    paintRowsCommon(p, rect, rowCount, curRow, topRow,
        [host](int i){ return host ? host->playlistRowText(i)
                                    : QString(); },
        [host](int i){
            return host ? formatDuration(host->playlistRowDurationMs(i))
                        : QString();
        },
        [curRow](int i) {
            return (i == curRow) ? QColor(255, 255, 255)
                                  : QColor(220, 225, 235);
        },
        QColor(28, 36, 50),
        QColor(58, 59, 82, 220));
}

void paintLibraryRows(QPainter *p, PaintCtx &ctx,
                      const QRect &rect, int &topRow,
                      const QString &parentPath,
                      QHash<QString, bool> &expansion) {
    Host *host = ctx.host;
    if (!host) {
        paintRowsCommon(p, rect, 0, -1, topRow, {}, {}, {},
                         QColor(28, 36, 50),
                         QColor(58, 59, 82, 220));
        return;
    }
    // Build the visible flat row list by walking the tree DFS,
    // honouring `expansion` for which directories descend.  Each
    // row carries its display path and depth.
    struct Row { QString label; QString path; int depth; bool isDir; bool expanded; };
    QList<Row> rows;
    std::function<void(const QString &, int)> walk =
        [&](const QString &parent, int depth) {
            const int n = host->libraryRowCount(parent);
            for (int i = 0; i < n; ++i) {
                const QString lbl  = host->libraryRowLabel(parent, i);
                const QString path = host->libraryRowPath(parent, i);
                const bool dir = host->libraryRowHasChildren(parent, i);
                const bool exp = dir && expansion.value(path, false);
                rows.append({lbl, path, depth, dir, exp});
                if (exp) walk(path, depth + 1);
            }
        };
    walk(parentPath, 0);

    paintRowsCommon(p, rect, rows.size(), -1, topRow,
        [&rows](int i){
            const auto &r = rows[i];
            const QString prefix = QString(r.depth * 2, QChar(' ')) +
                (r.isDir ? (r.expanded ? QStringLiteral("- ")
                                        : QStringLiteral("+ "))
                          : QStringLiteral("  "));
            return prefix + r.label;
        },
        nullptr,
        [&rows](int i){
            return rows[i].isDir ? QColor(220, 230, 255)
                                  : QColor(200, 205, 215);
        },
        QColor(28, 36, 50),
        QColor(58, 59, 82, 220));
}

void PlaylistProWidget::paint(QPainter *p, PaintCtx &ctx,
                              const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;
    // Record canvas-space bbox so onLeftButtonDown can map clicks
    // back to row indices.  resolveRect is parent-local; we map
    // through QPainter's current transform to get canvas coords.
    m_lastListRect = QRect(p->transform().map(r.topLeft()), r.size());

    paintPlaylistRows(p, ctx, r, m_topRow);

    QFont qf(QStringLiteral("sans-serif"));
    qf.setPixelSize(10);
    const QFontMetrics fm(qf);
    m_lastRowH = qMax(11, fm.height() + 1);

    lastPaintedAtMs = QDateTime::currentMSecsSinceEpoch();
}

void PlaylistProWidget::onLeftButtonDown(QPoint pos, PaintCtx &ctx) {
    if (!ctx.host || m_lastListRect.isEmpty()) return;
    const int relY = pos.y() - m_lastListRect.y() - 2;
    if (relY < 0) return;
    const int row = m_topRow + relY / m_lastRowH;
    if (row < 0 || row >= ctx.host->playlistRowCount()) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastClickRow == row && now - m_lastClickMs < 350) {
        ctx.host->playlistPlayRow(row);
        m_lastClickMs = 0;
        m_lastClickRow = -1;
    } else {
        ctx.host->playlistSetCurrentRow(row);
        m_lastClickMs = now;
        m_lastClickRow = row;
    }
    requestRepaint();
}

void PlaylistDirectoryWidget::paint(QPainter *p, PaintCtx &ctx,
                                     const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;
    m_lastListRect = QRect(p->transform().map(r.topLeft()), r.size());

    paintLibraryRows(p, ctx, r, m_topRow, m_parentPath, m_expansion);

    QFont qf(QStringLiteral("sans-serif"));
    qf.setPixelSize(10);
    const QFontMetrics fm(qf);
    m_lastRowH = qMax(11, fm.height() + 1);

    lastPaintedAtMs = QDateTime::currentMSecsSinceEpoch();
}

void PlaylistDirectoryWidget::onLeftButtonDown(QPoint pos,
                                                PaintCtx &ctx) {
    if (!ctx.host || m_lastListRect.isEmpty()) return;
    const int relY = pos.y() - m_lastListRect.y() - 2;
    if (relY < 0) return;
    const int visibleRow = m_topRow + relY / m_lastRowH;
    // Rebuild the same DFS the painter used, find row #visibleRow,
    // then toggle expansion on directories.
    Host *host = ctx.host;
    int idx = 0;
    std::function<bool(const QString &, int)> walk =
        [&](const QString &parent, int) -> bool {
            const int n = host->libraryRowCount(parent);
            for (int i = 0; i < n; ++i) {
                const QString path = host->libraryRowPath(parent, i);
                const bool isDir = host->libraryRowHasChildren(parent, i);
                const bool exp = isDir && m_expansion.value(path, false);
                if (idx++ == visibleRow) {
                    if (isDir) {
                        m_expansion[path] = !exp;
                    } else {
                        // Library leaf (a track): no host method exists
                        // to add a library entry to the playlist, so
                        // clicking a leaf does nothing and the Library
                        // tab is read-only.
                    }
                    requestRepaint();
                    return true;
                }
                if (exp && walk(path, 0)) return true;
            }
            return false;
        };
    walk(m_parentPath, 0);
}

}  // namespace qtWasabi
