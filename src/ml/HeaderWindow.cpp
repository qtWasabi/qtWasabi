// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "HeaderWindow.h"

#include <memory>

namespace qtWasabi {
namespace ml {

LRESULT HeaderWindow::wndProc(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case HDM_GETITEMCOUNT:
            return m_columns.size();
        case HDM_INSERTITEMW: {
            auto *item = reinterpret_cast<HDITEMW *>(lp);
            if (!item) return -1;
            HdrColumn c;
            if (item->mask & HDI_TEXT && item->pszText)
                c.text = QString::fromWCharArray(item->pszText);
            if (item->mask & HDI_WIDTH) c.width = item->cxy;
            if (item->mask & HDI_FORMAT) c.fmt = item->fmt;
            const int at = static_cast<int>(wp);
            int returnedIndex;
            if (at >= 0 && at < m_columns.size()) {
                m_columns.insert(at, c);
                returnedIndex = at;
            } else {
                m_columns.append(c);
                returnedIndex = m_columns.size() - 1;
            }
            return returnedIndex;
        }
        case HDM_DELETEITEM: {
            const int at = static_cast<int>(wp);
            if (at < 0 || at >= m_columns.size()) return FALSE;
            m_columns.removeAt(at);
            return TRUE;
        }
        case HDM_GETITEMW: {
            auto *item = reinterpret_cast<HDITEMW *>(lp);
            const int at = static_cast<int>(wp);
            if (!item || at < 0 || at >= m_columns.size()) return FALSE;
            if (item->mask & HDI_WIDTH)  item->cxy = m_columns[at].width;
            if (item->mask & HDI_FORMAT) item->fmt = m_columns[at].fmt;
            // pszText fill-in is up to the caller's buffer size;
            // gen_ml only reads width/fmt back so we skip the
            // wide-string copy.
            return TRUE;
        }
        case HDM_SETITEMW: {
            auto *item = reinterpret_cast<HDITEMW *>(lp);
            const int at = static_cast<int>(wp);
            if (!item || at < 0 || at >= m_columns.size()) return FALSE;
            if (item->mask & HDI_WIDTH)  m_columns[at].width = item->cxy;
            if (item->mask & HDI_FORMAT) m_columns[at].fmt   = item->fmt;
            if (item->mask & HDI_TEXT && item->pszText)
                m_columns[at].text = QString::fromWCharArray(item->pszText);
            return TRUE;
        }
        case HDM_GETITEMRECT: {
            auto *r = reinterpret_cast<RECT *>(lp);
            const int at = static_cast<int>(wp);
            if (!r || at < 0 || at >= m_columns.size()) return FALSE;
            int x = 0;
            for (int i = 0; i < at; ++i) x += m_columns[i].width;
            r->left   = x;
            r->top    = 0;
            r->right  = x + m_columns[at].width;
            r->bottom = 18;
            return TRUE;
        }
        default:
            return 0;
    }
}

HWND createHeader(HWND parent) {
    using namespace qtWasabi::wasabi_compat;
    auto h = std::make_unique<HeaderWindow>();
    h->parent = parent;
    return registerHandle<WindowObject>(std::move(h));
}

}  // namespace ml
}  // namespace qtWasabi
