#pragma once
//
// ListViewWindow — wasabi-compat WindowObject subclass backing an
// ML plugin's IDC_CURRENTVIEW ListView (LVS_REPORT mode) with a
// qtWasabi `MultiColumnListWidget`.
//
// The bridge: a plugin calls
//   ListView_InsertColumn(hwndLV, 0, &LV_COLUMN{..text=L"Artist"});
//   ListView_InsertItem(hwndLV,  &LV_ITEM{..pszText=L"All artists"});
// Both expand to SendMessageW(hwndLV, LVM_*, …); the handle registry
// routes to this subclass's wndProc, which translates the payloads
// onto the embedded MultiColumnListWidget.
//

#include "../widgets/MultiColumnList.h"

#include <handle-registry.h>
#include <winuser.h>
#include <commctrl.h>

#include <QString>
#include <QStringList>

namespace qtWasabi {
namespace ml {

class ListViewWindow : public qtWasabi::wasabi_compat::WindowObject {
public:
    ListViewWindow() = default;

    qtWasabi::MultiColumnListWidget &widget() { return m_list; }

    LRESULT wndProc(UINT msg, WPARAM wp, LPARAM lp) override;

    void paint(QPainter *p) override;

private:
    qtWasabi::MultiColumnListWidget m_list;
    DWORD                            m_extStyle = 0;
};

HWND createListView(HWND parent);

}  // namespace ml
}  // namespace qtWasabi
