// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "TreeList.h"

#include <qtWasabi/ColorRegistry.h>
#include <qtWasabi/GammasetRegistry.h>
#include <qtWasabi/PaintCtx.h>

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>

namespace qtWasabi {

namespace {

QColor themed(PaintCtx &ctx, const char *id, QColor fallback) {
    if (!ctx.colors) return fallback;
    return ctx.colors->resolve(QString::fromLatin1(id),
                                ctx.gammasets, fallback);
}

}  // anonymous

void TreeListWidget::setRoots(QList<TreeListNode> roots) {
    m_roots = std::move(roots);
    m_expanded.clear();
    for (const TreeListNode &n : m_roots) {
        if (n.defaultExpanded) m_expanded.insert(n.invariantId);
    }
    if (m_selection < 0 || m_selection >= 1)
        m_selection = 0;
}

void TreeListWidget::setSelection(int row) {
    m_selection = row;
    requestRepaint();
}

QList<TreeListWidget::VisibleRow> TreeListWidget::flattenVisible() const {
    QList<VisibleRow> out;
    std::function<void(const TreeListNode &, int)> walk =
        [&](const TreeListNode &node, int depth) {
            const bool hasChildren = static_cast<bool>(node.childProvider);
            const bool expanded =
                hasChildren && m_expanded.contains(node.invariantId);
            out.append({node, depth, hasChildren, expanded});
            if (expanded) {
                const QList<TreeListNode> kids = node.childProvider();
                for (const TreeListNode &k : kids) walk(k, depth + 1);
            }
        };
    for (const TreeListNode &n : m_roots) walk(n, 0);
    return out;
}

void TreeListWidget::paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    if (attrs.value(QStringLiteral("alpha")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;
    m_lastRect = QRect(p->transform().map(r.topLeft()), r.size());

    // Theme colour resolution via the CANONICAL wa_dlg `wasabi.list.*`
    // element names (the same ones gen_ml's tree reads from the genex),
    // so the tree is themed correctly for ANY skin — Bento resolves
    // these to its near-black list background + light text.  The dead
    // `color.ml.list.*` ids used here before never resolved.
    auto first = [&](std::initializer_list<const char *> ids, QColor fb) {
        for (const char *id : ids) {
            QColor c = themed(ctx, id, QColor());
            if (c.isValid()) return c;
        }
        return fb;
    };
    const QColor bg     = first({"wasabi.list.background"}, QColor(8, 9, 10));
    const QColor text   = first({"wasabi.list.text", "wasabi.window.text"},
                                 QColor(210, 210, 210));
    const QColor selBg  = first({"wasabi.list.text.selected.background",
                                  "wasabi.list.item.selected"},
                                 QColor(58, 80, 140));
    const QColor selFg  = first({"wasabi.list.text.selected"},
                                 QColor(255, 255, 255));
    const QColor twistC = first({"wasabi.list.text", "wasabi.window.text"},
                                 QColor(160, 165, 175));

    p->save();
    p->fillRect(r, bg);

    QFont rowFont(QStringLiteral("Tahoma"));
    rowFont.setPixelSize(11);
    const QFontMetrics rowFm(rowFont);
    const int rowH = qMax(14, rowFm.height() + 2);

    const QList<VisibleRow> visible = flattenVisible();
    m_lastVisible   = visible;
    m_lastRowH      = rowH;
    m_lastFirstRowY = r.y() + 2;

    int y = r.y() + 2;
    for (int i = 0; i < visible.size(); ++i) {
        const VisibleRow &vr = visible[i];
        if (y + rowH > r.bottom()) break;
        const QRect row(r.x(), y, r.width(), rowH);
        const bool selected = (i == m_selection);
        if (selected) p->fillRect(row, selBg);

        // Base indent leaves a 4px twist gutter INSIDE the left border:
        // the twist sits at row.x()+4 (was row.x()-4, overflowing the
        // sidebar's sunken border) and the icon/text follow at +16.
        const int indentPx = 16 + vr.depth * 12;

        // Twist triangle for folder rows.  We don't load a bitmap
        // — paint a small ▶/▼ filled triangle directly so any
        // skin without `tree_open`/`tree_closed` bitmaps still
        // shows expand state visually.
        if (vr.isFolder) {
            const int tx = row.x() + indentPx - 12;
            const int ty = row.y() + (rowH - 8) / 2;
            QPolygon poly;
            if (vr.isExpanded) {
                poly << QPoint(tx,     ty + 1)
                     << QPoint(tx + 8, ty + 1)
                     << QPoint(tx + 4, ty + 6);
            } else {
                poly << QPoint(tx + 1, ty)
                     << QPoint(tx + 6, ty + 4)
                     << QPoint(tx + 1, ty + 8);
            }
            p->setBrush(selected ? selFg : twistC);
            p->setPen(Qt::NoPen);
            p->drawPolygon(poly);
        }

        int textLeft = row.x() + indentPx;

        // Icon resolution.  Bitmap resolver takes precedence; if
        // the resolved image is null and the resource string looks
        // like a file path, try a direct QImage load as a last
        // resort.
        QImage icon;
        if (!vr.node.iconResource.isEmpty()) {
            if (m_iconResolver) icon = m_iconResolver(vr.node.iconResource);
            if (icon.isNull() &&
                (vr.node.iconResource.startsWith(QLatin1Char('/')) ||
                 vr.node.iconResource.endsWith(QLatin1String(".png")) ||
                 vr.node.iconResource.endsWith(QLatin1String(".bmp")))) {
                icon = QImage(vr.node.iconResource);
            }
        }
        if (!icon.isNull()) {
            const int iy = row.y() + (rowH - 16) / 2;
            p->drawImage(QRect(textLeft, iy, 16, 16), icon);
            textLeft += 18;
        }

        p->setPen(selected ? selFg : text);
        p->setFont(rowFont);
        p->drawText(QRect(textLeft, row.y(),
                           row.right() - textLeft - 2, rowH),
                    Qt::AlignVCenter | Qt::AlignLeft,
                    vr.node.displayLabel);

        y += rowH;
    }
    p->restore();
}

void TreeListWidget::onLeftButtonDown(QPoint pos, PaintCtx &) {
    if (m_lastRect.isEmpty() || m_lastRowH <= 0) return;
    if (!m_lastRect.contains(pos)) return;
    const int relY = pos.y() - m_lastFirstRowY;
    if (relY < 0) return;
    const int hit = relY / m_lastRowH;
    if (hit < 0 || hit >= m_lastVisible.size()) return;
    const VisibleRow &vr = m_lastVisible[hit];

    // Click on the twist triangle area (first ~14 px of the row's
    // indent gutter) toggles the folder.  Everywhere else selects.
    const int rowLeftX = m_lastRect.x();
    const int twistRight = rowLeftX + 6 + vr.depth * 12;
    if (vr.isFolder && pos.x() < twistRight) {
        if (m_expanded.contains(vr.node.invariantId))
            m_expanded.remove(vr.node.invariantId);
        else
            m_expanded.insert(vr.node.invariantId);
    } else {
        m_selection = hit;
    }
    requestRepaint();
}

}  // namespace qtWasabi
