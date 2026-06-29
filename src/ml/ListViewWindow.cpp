// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "ListViewWindow.h"

#include <memory>

namespace qtWasabi {
namespace ml {

LRESULT ListViewWindow::wndProc(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case LVM_INSERTCOLUMNW: {
            auto *col = reinterpret_cast<LVCOLUMNW *>(lp);
            if (!col) return -1;
            const int wantIdx = static_cast<int>(wp);
            QString label;
            int width = 80;
            int align = 0;
            if (col->mask & LVCF_TEXT && col->pszText) {
                label = QString::fromWCharArray(col->pszText);
            }
            if (col->mask & LVCF_WIDTH)   width = col->cx;
            if (col->mask & LVCF_FMT) {
                switch (col->fmt & LVCFMT_JUSTIFYMASK) {
                    case LVCFMT_CENTER: align = 1; break;
                    case LVCFMT_RIGHT:  align = 2; break;
                    default:            align = 0; break;
                }
            }
            // We honour append-order: the ML host inserts columns at
            // index 0, 1, 2 sequentially, so wantIdx only informs sort.
            (void)wantIdx;
            const int idx = m_list.appendColumn(label, width, align);
            return idx;
        }
        case LVM_INSERTITEMW: {
            auto *item = reinterpret_cast<LVITEMW *>(lp);
            if (!item) return -1;
            const int wantRow = item->iItem;
            (void)wantRow;
            // Gather cell strings.  Subitems arrive later via
            // LVM_SETITEMTEXTW, so this initial insert only fills
            // column 0.
            QStringList cells;
            const int ncols = m_list.columnCount();
            for (int c = 0; c < ncols; ++c) cells.append(QString());
            if (item->mask & LVIF_TEXT && item->pszText && ncols > 0) {
                cells[0] = QString::fromWCharArray(item->pszText);
            }
            m_list.appendRow(cells);
            return m_list.rowCount() - 1;
        }
        case LVM_SETITEMTEXTW: {
            // wp = item index, lp = LV_ITEM* with iSubItem + pszText
            auto *item = reinterpret_cast<LVITEMW *>(lp);
            if (!item) return FALSE;
            // MultiColumnListWidget has no cell-text setter, so we
            // accept the call silently.  The ML host's owner-data mode
            // computes most cells lazily via LVN_GETDISPINFO anyway.
            (void)wp;
            return TRUE;
        }
        case LVM_DELETEITEM:
            // MultiColumnListWidget has no single-row remove, so this
            // is a no-op.  The ML host clears the list almost entirely
            // via LVM_DELETEALLITEMS.
            return FALSE;
        case LVM_DELETEALLITEMS:
            m_list.clearRows();
            return TRUE;
        case LVM_GETITEMCOUNT:
            return m_list.rowCount();
        case LVM_GETCOLUMNWIDTH:
            (void)wp; (void)lp;
            return 80;  // fixed baseline; per-column width is not tracked
        case LVM_SETCOLUMNWIDTH:
            // MultiColumnListWidget has no column-resize setter, so we
            // accept the call silently.
            return TRUE;
        case LVM_SETEXTENDEDLISTVIEWSTYLE:
            m_extStyle = static_cast<DWORD>(lp);
            return TRUE;
        case LVM_GETEXTENDEDLISTVIEWSTYLE:
            return m_extStyle;
        case LVM_SETITEMSTATE: {
            // Track selection via the LVIS_SELECTED state bit.
            auto *item = reinterpret_cast<LVITEMW *>(lp);
            if (!item) return FALSE;
            const int row = static_cast<int>(wp);
            if (item->stateMask & LVIS_SELECTED) {
                if (item->state & LVIS_SELECTED) m_list.setSelection(row);
            }
            return TRUE;
        }
        case LVM_GETITEMSTATE:
            return (m_list.selection() == static_cast<int>(wp))
                ? LVIS_SELECTED : 0;
        default:
            return 0;
    }
}

void ListViewWindow::paint(QPainter * /*p*/) {
    // No-op: the ML host renderer paints the embedded widget directly
    // during the windowholder's paint pass, not through this entry.
}

HWND createListView(HWND parent) {
    using namespace qtWasabi::wasabi_compat;
    auto lv = std::make_unique<ListViewWindow>();
    lv->parent = parent;
    return registerHandle<WindowObject>(std::move(lv));
}

}  // namespace ml
}  // namespace qtWasabi
