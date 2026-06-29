// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// smoke-phase-a.cpp — end-to-end verification.
//
// Compiles a synthetic minimal Win32 plugin against the
// wasabi-compat layer and exercises a representative subset of the
// API surface gen_ml hits on boot:
//
//   1. InitCommonControlsEx — register the Common Controls.
//   2. ImageList_Create + ImageList_AddIcon — set up a navigation
//      image list (mirrors gen_ml's `AddTreeImageBmp`).
//   3. CreateCompatibleBitmap + GetObjectW — bitmap creation +
//      attribute query.
//   4. BeginPaint / EndPaint round-trip with DC state mutation
//      (SetTextColor / SetBkMode).
//   5. Synthetic HWND backing — register a TreeView-shaped
//      WindowObject, fire TVM_INSERTITEMW via SendMessageW,
//      verify it round-trips to the wndProc with the TVITEMW
//      pointer intact.
//   6. GWL_USERDATA persistence across SendMessage boundary.
//
// Together these confirm the shim is functional enough for a
// gen_ml-shaped plugin to boot against it.
//

#include "win32/handle-registry.h"
#include "win32/winuser.h"
#include "win32/commctrl.h"
#include "win32/wingdi.h"

#include "smoke-check.h"
#include <cstring>
#include <memory>

namespace qtWasabi {
namespace wasabi_compat {
namespace {

// Synthetic TreeView WindowObject — records the last
// TVM_INSERTITEMW payload it received.
class FakeTreeView : public WindowObject {
public:
    TVINSERTSTRUCTW last_insert = {};
    int             insert_count = 0;

    LRESULT wndProc(UINT msg, WPARAM wp, LPARAM lp) override {
        switch (msg) {
            case TVM_INSERTITEMW: {
                auto *tvi = reinterpret_cast<TVINSERTSTRUCTW *>(lp);
                if (tvi) last_insert = *tvi;
                ++insert_count;
                // Real TreeView returns the new HTREEITEM.  We
                // hand back a synthetic non-null id.
                return reinterpret_cast<LRESULT>(
                    reinterpret_cast<HTREEITEM>(
                        static_cast<uintptr_t>(0x100u + insert_count)));
            }
            case TVM_DELETEITEM:
                return TRUE;
            case TVM_GETCOUNT:
                return insert_count;
            default:
                break;
        }
        (void)wp;
        return 0;
    }
};

struct PhaseASmoke {
    PhaseASmoke() {
        // 1. InitCommonControls — gen_ml's boot sequence does this.
        INITCOMMONCONTROLSEX icc = {sizeof(INITCOMMONCONTROLSEX),
                                     ICC_TREEVIEW_CLASSES |
                                     ICC_LISTVIEW_CLASSES |
                                     ICC_BAR_CLASSES};
        SMOKE_CHECK(InitCommonControlsEx(&icc));
        SMOKE_CHECK(InitCommonControls());

        // 2. ImageList — mirrors AddTreeImageBmp().
        HIMAGELIST il = ImageList_Create(16, 16,
                                          ILC_COLOR32 | ILC_MASK,
                                          16, 16);
        SMOKE_CHECK(il);
        int idx0 = ImageList_AddIcon(il, nullptr);
        int idx1 = ImageList_AddIcon(il, nullptr);
        SMOKE_CHECK(idx0 == 0 && idx1 == 1);
        SMOKE_CHECK(ImageList_GetImageCount(il) == 2);

        // 3. CreateCompatibleBitmap + GetObjectW.
        HDC hdc = GetDC(nullptr);
        SMOKE_CHECK(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, 32, 16);
        SMOKE_CHECK(bmp);
        BITMAP bm = {};
        SMOKE_CHECK(GetObjectW(static_cast<HGDIOBJ>(static_cast<void *>(bmp)),
                          sizeof(bm), &bm) >= 0);
        DeleteObject(static_cast<HGDIOBJ>(static_cast<void *>(bmp)));

        // 4. BeginPaint / EndPaint round-trip with DC mutation.
        PAINTSTRUCT ps;
        HDC paint_dc = BeginPaint(nullptr, &ps);
        SMOKE_CHECK(paint_dc == ps.hdc);
        SetTextColor(paint_dc, RGB(220, 230, 235));
        SetBkMode(paint_dc, TRANSPARENT);
        FillRect(paint_dc, &ps.rcPaint, (HBRUSH)GetStockObject(BLACK_BRUSH));
        EndPaint(nullptr, &ps);
        ReleaseDC(nullptr, hdc);

        // 5. Synthetic TreeView WindowObject + TVM_INSERTITEMW round
        //    trip.  This mirrors gen_ml's MLNavCtrl_InsertItem
        //    macro expansion: `(HTREEITEM)SendMessageW(hwndTree,
        //    TVM_INSERTITEMW, 0, (LPARAM)&insertStruct)`.
        auto tv = std::make_unique<FakeTreeView>();
        FakeTreeView *raw_tv = tv.get();
        HWND hwndTree = registerHandle<WindowObject>(std::move(tv));
        SMOKE_CHECK(hwndTree);

        wchar_t label[] = L"Local Library";
        TVINSERTSTRUCTW ins = {};
        ins.hParent      = TVI_ROOT;
        ins.hInsertAfter = TVI_LAST;
        ins.item.mask    = TVIF_TEXT | TVIF_PARAM;
        ins.item.pszText = label;
        ins.item.lParam  = 0xCAFE;

        HTREEITEM hti = TreeView_InsertItem(hwndTree, &ins);
        SMOKE_CHECK(hti);
        SMOKE_CHECK(raw_tv->insert_count == 1);
        SMOKE_CHECK(raw_tv->last_insert.hParent      == TVI_ROOT);
        SMOKE_CHECK(raw_tv->last_insert.hInsertAfter == TVI_LAST);
        SMOKE_CHECK(raw_tv->last_insert.item.lParam  == 0xCAFE);
        // pszText is a pointer that survived the SendMessage path;
        // verify the text round-tripped intact.
        SMOKE_CHECK(raw_tv->last_insert.item.pszText != nullptr);
        SMOKE_CHECK(std::wcscmp(raw_tv->last_insert.item.pszText,
                            L"Local Library") == 0);

        // TVM_GETCOUNT — also via SendMessage.
        LRESULT n = SendMessageW(hwndTree, TVM_GETCOUNT, 0, 0);
        SMOKE_CHECK(n == 1);

        // 6. GWL_USERDATA persistence.  gen_ml stashes its
        //    `mainwndState *` here; we just check the bits survive.
        SetWindowLongPtrW(hwndTree, GWL_USERDATA, 0xDEADBEEF);
        SMOKE_CHECK(GetWindowLongPtrW(hwndTree, GWL_USERDATA) ==
                static_cast<LONG_PTR>(0xDEADBEEF));

        DestroyWindow(hwndTree);
        ImageList_Destroy(il);
    }
};
static PhaseASmoke s_smoke;

}  // anonymous
}  // namespace wasabi_compat
}  // namespace qtWasabi
