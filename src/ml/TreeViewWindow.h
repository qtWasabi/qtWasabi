#pragma once
//
// TreeViewWindow — wasabi-compat WindowObject subclass that backs
// gen_ml's IDC_NAVIGATION TreeView control with a real qtWasabi
// `TreeListWidget`.
//
// gen_ml does
//   TreeView_InsertItem(hwndTree, &TVINSERTSTRUCTW{TVI_ROOT, …,
//                                                   pszText=L"Now Playing"})
// which expands to `SendMessageW(hwndTree, TVM_INSERTITEMW, 0, …)`.
// wasabi-compat's HWND registry routes the SendMessage to the
// WindowObject's `wndProc`; this subclass interprets the TVM_*
// payloads and mutates its TreeListWidget.
//
// HTREEITEM identity: each inserted item gets a process-unique id
// stored in a local `m_items` table.  `HTREEITEM` is the registry-
// pointer encoding (high 32 bits TreeViewWindow id, low 32 item
// id) so two distinct TreeViews can never confuse their items.
// gen_ml hands the HTREEITEM back via `lParam` on subsequent
// `TVM_SELECTITEM` / `TVM_DELETEITEM` calls; we decode and look
// up in O(1).
//
// Paint integration: TreeViewWindow doesn't paint itself directly
// — its parent (gen_ml host) is a HolderRenderer that asks the
// embedded TreeListWidget to paint at its allocated rect.  The
// WindowObject's `paint(QPainter*)` override delegates straight
// through.
//

#include "../widgets/TreeList.h"

#include <handle-registry.h>
#include <winuser.h>
#include <commctrl.h>

#include <QHash>
#include <QList>
#include <QString>

namespace qtWasabi {
namespace ml {

// Per-item record.  Keeps the TreeListNode the widget renders,
// plus the gen_ml-supplied lParam so callers can round-trip
// their context pointer through SendMessage.
struct TvItem {
    quint32       id          = 0;
    QString       invariantId;   // matches TreeListNode::invariantId
    QString       label;
    LPARAM        userLParam   = 0;
    HTREEITEM     hParent      = nullptr;  // TVI_ROOT for top-level
    int           iImage       = -1;
    int           iSelectedImage = -1;
    bool          hasChildren  = false;
};

class TreeViewWindow : public qtWasabi::wasabi_compat::WindowObject {
public:
    TreeViewWindow();

    // The TreeListWidget the gen_ml-host parent paints.  Owned
    // here; lifetime tied to the WindowObject.
    qtWasabi::TreeListWidget &widget() { return m_tree; }

    // SendMessage dispatch.
    LRESULT wndProc(UINT msg, WPARAM wp, LPARAM lp) override;

    // Composite into the host's QPainter — delegates to the
    // embedded TreeListWidget at the WindowObject's rect.
    void paint(QPainter *p) override;

private:
    qtWasabi::TreeListWidget m_tree;

    // HTREEITEM registry.  Each inserted item has a process-
    // unique 32-bit id; HTREEITEM is the registry-pointer
    // encoding (low 32 bits = item id, high 32 bits = TreeView
    // instance tag).  Items hold their own parent ptr.
    QHash<quint32, TvItem>   m_items;
    quint32                  m_nextId      = 1;
    quint32                  m_instanceTag = 0;

    // Cached re-builder.  Whenever a TVM_INSERTITEMW / DELETE
    // mutates m_items, we rebuild the TreeListWidget's roots
    // from m_items.
    void rebuildRoots();

    // Helper: encode (instance, id) → HTREEITEM and decode.
    HTREEITEM encode(quint32 id) const;
    quint32   decode(HTREEITEM h) const;
};

// Factory — registers the TreeViewWindow with wasabi-compat's
// HWND registry and returns the opaque HWND for gen_ml-style
// SendMessage callers.  The parent HWND is recorded so child
// notifications (`WM_NOTIFY` with NMHDR.hwndFrom) reach the
// right receiver.
HWND createTreeView(HWND parent);

}  // namespace ml
}  // namespace qtWasabi
