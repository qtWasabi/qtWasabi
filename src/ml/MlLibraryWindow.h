#pragma once
//
// MlLibraryWindow — the WindowObject backing the media-library
// "library HWND" sentinel.  Every ml_* plugin's `MLNavCtrl_*` macro
// expands to `SendMessageW(hwndLibrary, WM_ML_IPC, payload,
// ml_ipc_id)`.  This subclass interprets the LPARAM-tagged
// dispatch and translates onto an embedded TreeListWidget (via
// the TreeViewWindow infrastructure).
//
// `singletonHwnd()` returns the HWND this class owns; plugin TUs
// grab it once at init to discover the library window.
//

#include "TreeViewWindow.h"
#include <handle-registry.h>
#include <winuser.h>
#include <ml_ipc.h>

#include <QHash>

namespace qtWasabi {
namespace ml {

class MlLibraryWindow : public qtWasabi::wasabi_compat::WindowObject {
public:
    MlLibraryWindow();
    ~MlLibraryWindow() override;

    LRESULT wndProc(UINT msg, WPARAM wp, LPARAM lp) override;

    // Embedded nav tree the LIBRARY HWND forwards ML_IPC_NAVCTRL_*
    // calls to.  MlHostRenderer is what visually paints the tree;
    // this class just owns the data model.
    TreeViewWindow &nav() { return m_nav; }

    // Process-wide singleton HWND.  Plugins call into here exactly
    // once at init to discover the media-library window.
    static HWND singletonHwnd();

private:
    TreeViewWindow m_nav;
    HIMAGELIST     m_imageList = nullptr;
};

// Process-lifetime accessor — first call constructs the singleton
// MlLibraryWindow and registers it with the HWND registry.
HWND ensureLibraryWindow();

// dlopens every ml_* plugin `.so` found in the plugin search dirs,
// populates each one's `hwndLibraryParent` / parent HWND fields, and
// invokes its `init()`.  After this returns, plugins have had the
// chance to call MLNavCtrl_InsertItem and friends.  Logs init return
// codes to stderr.  The whole load runs at most once per process; a
// second call is a no-op.
void loadBuiltinMlPlugins();

}  // namespace ml
}  // namespace qtWasabi
