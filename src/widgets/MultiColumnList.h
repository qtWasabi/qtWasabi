#pragma once
//
// MultiColumnListWidget — sortable column-header bar + uniform
// row body.  The "ListView in report mode" Win32 equivalent that
// gen_ml builds its three content panes from (Artist/Albums/Tracks,
// Album/Year/Tracks, and the final track grid).
//
// Renders the header + rows, click-sort, and horizontal-scrollable
// rows.  LVM_INSERTCOLUMNW / LVM_INSERTITEMW from wasabi-compat feed
// the column + row vectors.
//
// Data model: each column carries label + width + alignment + an
// optional sort indicator state (asc/desc/none).  Rows are flat
// vectors of cell strings keyed by column index.  Per-cell styling
// is not yet supported (NM_CUSTOMDRAW would let plugins paint
// arbitrary cells).
//

#include <qtWasabi/Widget.h>

#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>

namespace qtWasabi {

struct MclColumn {
    QString label;
    int     width    = 80;
    int     align    = 0;        // 0 left, 1 center, 2 right
    int     sortMode = 0;        // 0 none, 1 asc, -1 desc
};

class MultiColumnListWidget : public Widget {
public:
    MultiColumnListWidget() = default;

    // Column management.  appendColumn returns the new column's
    // index for the caller to track sort state through.
    int  appendColumn(const QString &label, int width,
                       int align = 0);
    void clearColumns();
    int  columnCount() const { return m_columns.size(); }

    // Row management.
    void appendRow(const QStringList &cells);
    void clearRows();
    int  rowCount() const { return m_rows.size(); }
    int  selection() const { return m_selection; }
    void setSelection(int row);

    // Canvas-space rect of the last paint (for the owner's hit routing).
    QRect lastCanvasRect() const { return m_lastRect; }

    // Whether this pane owns focus.  The focused pane draws its selected
    // row with the skin's active selection-bar colours; unfocused panes
    // dim it (WADLG_INACT_SELBAR), matching real ml_local multi-pane
    // focus behaviour.
    void setActive(bool a) { m_active = a; }

    bool isInteractive() const override { return true; }

    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
    void onLeftButtonDown(QPoint pos, PaintCtx &ctx) override;

private:
    QList<MclColumn>    m_columns;
    QList<QStringList>  m_rows;
    int                 m_selection = -1;
    bool                m_active    = true;

    // Cached geometry for click routing.
    QRect               m_lastRect;
    int                 m_lastHeaderH = 16;
    int                 m_lastRowH    = 14;
    int                 m_lastFirstRowY = 0;
};

}  // namespace qtWasabi
