// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// Verifies the WM_ML_IPC nav-control dispatch path.
//
// Mirrors what every ml_* plugin does at init: discover the
// library HWND, call `MLNavCtrl_InsertItem` with a
// `NAVINSERTSTRUCT`, get back an `HNAVITEM`, then iterate /
// select.  Exercising the WM_ML_IPC dispatch path end-to-end
// proves the wasabi-compat `ml_ipc.h` shim + the
// `MlLibraryWindow` receiver + the `TreeViewWindow` payload
// translation all line up.
//

#include "MlLibraryWindow.h"

#include <ml_ipc.h>

#include "smoke-check.h"
#include <cstring>

namespace qtWasabi {
namespace ml {
namespace {

struct PhaseESmoke {
    PhaseESmoke() {
        // 1. Discover (or create) the library HWND.  An ml_* plugin
        //    does this via `Plugin_GetLibrary()`; the same
        //    process-singleton lives behind our helper.
        HWND hLib = ensureLibraryWindow();
        SMOKE_CHECK(hLib);

        // 2. Version probe — every ml_* plugin queries the gen_ml
        //    interface version on init.  Our shim reports 0x031F.
        const DWORD ver = GetMLVersion(hLib);
        SMOKE_CHECK(ver == 0x031F);

        // 3. Image list — `MLNavCtrl_GetImageList` must return a
        //    non-null backing.
        HMLIMGLST iml = MLNavCtrl_GetImageList(hLib);
        SMOKE_CHECK(iml != nullptr);

        // 4. Insert a node — mirrors ml_nowplaying's
        //    `Navigation_CreateItem` path.
        wchar_t label[]     = L"Now Playing";
        wchar_t invariant[] = L"omNowPlayingXX";
        NAVINSERTSTRUCT nis = {};
        nis.hParent      = nullptr;            // NCI_FIRST root
        nis.hInsertAfter = NCI_LAST;
        nis.item.cbSize  = sizeof(NAVITEM);
        nis.item.mask    = NIMF_TEXT | NIMF_STYLE |
                           NIMF_TEXTINVARIANT | NIMF_PARAM;
        nis.item.pszText      = label;
        nis.item.pszInvariant = invariant;
        nis.item.style        = NIS_ALLOWCHILDMOVE;
        nis.item.styleMask    = nis.item.style;
        nis.item.lParam       = 0x12345678;

        HNAVITEM hItem = MLNavCtrl_InsertItem(hLib, &nis);
        SMOKE_CHECK(hItem != nullptr);

        // 5. Validate the embedded TreeListWidget reflects the
        //    insertion.  ml_nowplaying observes this via the
        //    plugin's notification chain; we just peek at the
        //    underlying widget.
        using namespace qtWasabi::wasabi_compat;
        MlLibraryWindow *lib =
            static_cast<MlLibraryWindow *>(lookupHandle<WindowObject>(hLib));
        SMOKE_CHECK(lib != nullptr);
        const auto &roots = lib->nav().widget().roots();
        // Other smokes may have inserted into the same tree by the
        // time we run; just assert our entry is present.
        bool found = false;
        for (const auto &n : roots) {
            if (n.displayLabel == QStringLiteral("Now Playing")) {
                found = true; break;
            }
        }
        SMOKE_CHECK(found);

        // 6. Selection round-trip.
        SMOKE_CHECK(MLNavItem_Select(hLib, hItem));
        HNAVITEM hSel = MLNavCtrl_GetSelection(hLib);
        SMOKE_CHECK(hSel == hItem);

        // 7. Deletion.  Run the call separately so NDEBUG builds
        //    don't optimise the actual SendMessage away with the
        //    assert wrapper.
        const INT delRv = MLNavCtrl_DeleteItem(hLib, hItem);
        SMOKE_CHECK(delRv == TRUE);
        (void)delRv;
        // After delete, the entry's gone.
        const auto &rootsAfter = lib->nav().widget().roots();
        for (const auto &n : rootsAfter) {
            SMOKE_CHECK(n.displayLabel != QStringLiteral("Now Playing"));
            (void)n;
        }
    }
};
static PhaseESmoke s_smoke;

}  // anonymous
}  // namespace ml
}  // namespace qtWasabi
