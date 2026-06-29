// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// End-to-end verification of the TreeView / ListView windows.
//
// A synthetic gen_ml-style plugin walks the canonical init shape:
//
//     HWND hwndTree = createTreeView(parent_hwnd);
//     HWND hwndList = createListView(parent_hwnd);
//
//     // ListView column setup (matches the Artist/Albums/Tracks
//     // pane gen_ml builds inside the windowholder slot).
//     LVCOLUMNW colArtist = {LVCF_TEXT | LVCF_WIDTH,
//                              LVCFMT_LEFT, 150, L"Artist"};
//     ListView_InsertColumn(hwndList, 0, &colArtist);
//     // …two more columns…
//
//     // Tree nodes — TVINSERTSTRUCTW is the gen_ml-side shape.
//     TVINSERTSTRUCTW ins = {TVI_ROOT, TVI_LAST,
//                              {TVIF_TEXT, …, L"Local Library", …}};
//     HTREEITEM hRoot = TreeView_InsertItem(hwndTree, &ins);
//     TVINSERTSTRUCTW kid = {hRoot, TVI_LAST,
//                              {TVIF_TEXT, …, L"Audio", …}};
//     TreeView_InsertItem(hwndTree, &kid);
//
//     // Insert a row.
//     LVITEMW row = {LVIF_TEXT, 0, 0, 0, 0, L"All (0 artists)"};
//     ListView_InsertItem(hwndList, &row);
//
// After all of which: TreeView_GetCount returns 2, the embedded
// TreeListWidget's roots reflect the parent/child shape; the
// ListView's MultiColumnListWidget has 3 columns + 1 row.
//

#include "TreeViewWindow.h"
#include "ListViewWindow.h"

#include "smoke-check.h"

namespace qtWasabi {
namespace ml {
namespace {

struct PhaseDSmoke {
    PhaseDSmoke() {
        using namespace qtWasabi::wasabi_compat;

        // Synthetic parent so child windows can record it.
        HWND parent = registerHandle<WindowObject>(
            std::make_unique<WindowObject>());
        SMOKE_CHECK(parent);

        // ── TreeView ──────────────────────────────────────────
        HWND hwndTree = createTreeView(parent);
        SMOKE_CHECK(hwndTree);
        SMOKE_CHECK(IsWindow(hwndTree));

        wchar_t labelLib[]   = L"Local Library";
        wchar_t labelAudio[] = L"Audio";
        wchar_t labelVideo[] = L"Video";

        TVINSERTSTRUCTW insLib = {};
        insLib.hParent      = TVI_ROOT;
        insLib.hInsertAfter = TVI_LAST;
        insLib.item.mask    = TVIF_TEXT | TVIF_PARAM;
        insLib.item.pszText = labelLib;
        insLib.item.lParam  = 0xA1;
        HTREEITEM hRoot = TreeView_InsertItem(hwndTree, &insLib);
        SMOKE_CHECK(hRoot != nullptr);

        TVINSERTSTRUCTW insAudio = {};
        insAudio.hParent      = hRoot;
        insAudio.hInsertAfter = TVI_LAST;
        insAudio.item.mask    = TVIF_TEXT;
        insAudio.item.pszText = labelAudio;
        HTREEITEM hAudio = TreeView_InsertItem(hwndTree, &insAudio);
        SMOKE_CHECK(hAudio != nullptr);

        TVINSERTSTRUCTW insVideo = {};
        insVideo.hParent      = hRoot;
        insVideo.hInsertAfter = TVI_LAST;
        insVideo.item.mask    = TVIF_TEXT;
        insVideo.item.pszText = labelVideo;
        HTREEITEM hVideo = TreeView_InsertItem(hwndTree, &insVideo);
        SMOKE_CHECK(hVideo != nullptr);
        // The three insertions yield distinct HTREEITEM values.
        SMOKE_CHECK(hRoot != hAudio && hAudio != hVideo && hRoot != hVideo);

        // Tree count round-trip.
        LRESULT count = SendMessageW(hwndTree, TVM_GETCOUNT, 0, 0);
        SMOKE_CHECK(count == 3);

        // Selection round-trip.  TreeView_SelectItem expands to
        // SendMessage(TVM_SELECTITEM, TVGN_CARET, hAudio).
        SMOKE_CHECK(TreeView_SelectItem(hwndTree, hAudio));

        // Validate the embedded TreeListWidget reflects the
        // inserts.  Tree has 1 root (Local Library) with 2
        // children (Audio + Video).
        TreeViewWindow *tv =
            static_cast<TreeViewWindow *>(lookupHandle<WindowObject>(hwndTree));
        SMOKE_CHECK(tv != nullptr);
        const auto &roots = tv->widget().roots();
        SMOKE_CHECK(roots.size() == 1);
        SMOKE_CHECK(roots[0].displayLabel == QStringLiteral("Local Library"));
        SMOKE_CHECK(static_cast<bool>(roots[0].childProvider));
        const auto kids = roots[0].childProvider();
        SMOKE_CHECK(kids.size() == 2);
        SMOKE_CHECK(kids[0].displayLabel == QStringLiteral("Audio"));
        SMOKE_CHECK(kids[1].displayLabel == QStringLiteral("Video"));

        // Deletion cascade — deleting hRoot drops all three.
        TreeView_DeleteItem(hwndTree, hRoot);
        SMOKE_CHECK(SendMessageW(hwndTree, TVM_GETCOUNT, 0, 0) == 0);

        DestroyWindow(hwndTree);

        // ── ListView ──────────────────────────────────────────
        HWND hwndList = createListView(parent);
        SMOKE_CHECK(hwndList);

        wchar_t artist[] = L"Artist";
        wchar_t albums[] = L"Albums";
        wchar_t tracks[] = L"Tracks";

        LVCOLUMNW c0 = {}; c0.mask = LVCF_TEXT | LVCF_WIDTH;
        c0.pszText = artist; c0.cx = 150;
        SMOKE_CHECK(ListView_InsertColumn(hwndList, 0, &c0) == 0);

        LVCOLUMNW c1 = {}; c1.mask = LVCF_TEXT | LVCF_WIDTH;
        c1.pszText = albums; c1.cx = 60;
        SMOKE_CHECK(ListView_InsertColumn(hwndList, 1, &c1) == 1);

        LVCOLUMNW c2 = {}; c2.mask = LVCF_TEXT | LVCF_WIDTH;
        c2.pszText = tracks; c2.cx = 60;
        SMOKE_CHECK(ListView_InsertColumn(hwndList, 2, &c2) == 2);

        ListViewWindow *lv =
            static_cast<ListViewWindow *>(lookupHandle<WindowObject>(hwndList));
        SMOKE_CHECK(lv != nullptr);
        SMOKE_CHECK(lv->widget().columnCount() == 3);

        wchar_t allArtists[] = L"All (0 artists)";
        LVITEMW item = {};
        item.mask     = LVIF_TEXT;
        item.iItem    = 0;
        item.pszText  = allArtists;
        SMOKE_CHECK(ListView_InsertItem(hwndList, &item) == 0);
        SMOKE_CHECK(ListView_GetItemCount(hwndList) == 1);
        SMOKE_CHECK(lv->widget().rowCount() == 1);

        ListView_DeleteAllItems(hwndList);
        SMOKE_CHECK(ListView_GetItemCount(hwndList) == 0);

        DestroyWindow(hwndList);
        DestroyWindow(parent);
    }
};
static PhaseDSmoke s_smoke;

}  // anonymous
}  // namespace ml
}  // namespace qtWasabi
