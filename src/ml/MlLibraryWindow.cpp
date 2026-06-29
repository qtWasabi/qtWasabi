// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "MlLibraryWindow.h"

#include <commctrl.h>

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <dlfcn.h>

#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QString>
#include <QStringList>

namespace qtWasabi {
namespace ml {

namespace {

std::atomic<HWND> g_libraryHwnd{nullptr};
std::mutex        g_libraryMu;

}  // anonymous

MlLibraryWindow::MlLibraryWindow() {
    // ml_* plugins call MLNavCtrl_GetImageList(hwndML) once at init.
    // Create the backing ImageList so the registration lookup
    // returns a non-null receiver; ml_nowplaying's icon adds land
    // here.
    m_imageList = ImageList_Create(16, 16,
                                     ILC_COLOR32 | ILC_MASK, 32, 32);
}

MlLibraryWindow::~MlLibraryWindow() {
    // Intentionally do NOT destroy m_imageList: static-init / exit
    // order across translation units is unspecified, so the
    // `ImageListObject` handle registry (in commctrl-stubs.cpp's
    // TU) may already have been destroyed by the time this dtor
    // runs at process exit.  The OS reclaims memory on exit so a
    // leak here is benign; explicit cleanup tripped a SIGSEGV in
    // `unordered_map::erase` against the already-dead registry.
}

LRESULT MlLibraryWindow::wndProc(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg != WM_ML_IPC) return 0;

    // LPARAM carries the ml_ipc sub-message id; WPARAM the
    // associated payload pointer / value.  Dispatch on the id.
    const LRESULT subMsg = lp;

    switch (subMsg) {
        case ML_IPC_GETVERSION:
            // Report the media-library interface version.  0x031F
            // (3.31) is the value the public 5.x ml_ipc interface
            // expects, so plugins that branch on it behave as if
            // talking to a current host.
            return 0x031F;

        case ML_IPC_NAVCTRL_GETIMAGELIST:
            return reinterpret_cast<LRESULT>(m_imageList);

        case ML_IPC_NAVCTRL_BEGINUPDATE:
        case ML_IPC_NAVCTRL_ENDUPDATE:
            // Batch-update guards.  Our TreeListWidget rebuilds its
            // roots on every insert regardless, so these are no-ops.
            // They exist only to satisfy plugins that bracket their
            // inserts in begin/end pairs.
            return 1;

        case ML_IPC_NAVCTRL_INSERTITEM: {
            // Translate the NAVINSERTSTRUCT → TVINSERTSTRUCTW and
            // route through our existing TreeViewWindow.  The two
            // structs carry the same conceptual data; only the
            // field names + the HNAVITEM/HTREEITEM aliases differ.
            auto *nis = reinterpret_cast<NAVINSERTSTRUCT *>(wp);
            if (!nis) return 0;

            TVINSERTSTRUCTW tv = {};
            tv.hParent      = reinterpret_cast<HTREEITEM>(nis->hParent);
            tv.hInsertAfter = TVI_LAST;
            tv.item.mask    = 0;
            if (nis->item.mask & NIMF_TEXT) {
                tv.item.mask    |= TVIF_TEXT;
                tv.item.pszText  = nis->item.pszText;
            }
            if (nis->item.mask & NIMF_PARAM) {
                tv.item.mask  |= TVIF_PARAM;
                tv.item.lParam = nis->item.lParam;
            }
            if (nis->item.mask & NIMF_IMAGE) {
                tv.item.mask  |= TVIF_IMAGE;
                tv.item.iImage = nis->item.iImage;
            }
            if (nis->item.mask & NIMF_IMAGESEL) {
                tv.item.mask           |= TVIF_SELECTEDIMAGE;
                tv.item.iSelectedImage  = nis->item.iSelectedImage;
            }

            // Forward to the embedded TreeViewWindow's existing
            // TVM_INSERTITEMW handler.
            HTREEITEM hti = reinterpret_cast<HTREEITEM>(
                m_nav.wndProc(TVM_INSERTITEMW, 0,
                                reinterpret_cast<LPARAM>(&tv)));
            // The ML IPC convention writes the assigned id back into
            // the NAVITEM when the caller passes id=0.
            if (nis->item.id == 0) {
                // We don't expose an id back through HNAVITEM:
                // callers use the HNAVITEM handle directly for
                // subsequent ops, and that handle is what we return.
            }
            return reinterpret_cast<LRESULT>(hti);
        }

        case ML_IPC_NAVCTRL_DELETEITEM: {
            auto h = reinterpret_cast<HTREEITEM>(wp);
            return m_nav.wndProc(TVM_DELETEITEM, 0,
                                   reinterpret_cast<LPARAM>(h));
        }

        case ML_IPC_NAVCTRL_GETSELECTION:
            return m_nav.wndProc(TVM_GETNEXTITEM, TVGN_CARET, 0);

        case ML_IPC_NAVITEM_SELECT: {
            auto h = reinterpret_cast<HTREEITEM>(wp);
            return m_nav.wndProc(TVM_SELECTITEM, TVGN_CARET,
                                   reinterpret_cast<LPARAM>(h));
        }

        // The remaining ML_IPC_NAVCTRL_* / ML_IPC_NAVITEM_* messages
        // (find-by-name, ensure-visible, get-rect, enum, move,
        // begin/end-edit-title) are not yet handled and fall through
        // to a no-op.  Each can be wired up when a plugin's tree
        // mutations require it.
        default:
            return 0;
    }
}

HWND MlLibraryWindow::singletonHwnd() {
    return g_libraryHwnd.load();
}

HWND ensureLibraryWindow() {
    HWND cur = g_libraryHwnd.load();
    if (cur) return cur;
    std::lock_guard<std::mutex> lk(g_libraryMu);
    cur = g_libraryHwnd.load();
    if (cur) return cur;
    using namespace qtWasabi::wasabi_compat;
    HWND h = registerHandle<WindowObject>(
        std::make_unique<MlLibraryWindow>());
    g_libraryHwnd.store(h);
    return h;
}

// ── loadBuiltinMlPlugins ───────────────────────────────────────
// Discover ml_* plugins via their `winampGetMediaLibraryPlugin`
// entry symbol and boot them.
//
// The Winamp media-library plugin ABI is described by the
// `winampMediaLibraryPlugin` struct.  We don't pull in its header
// here (that header tree drags in plenty of cruft we'd rather keep
// isolated to each plugin's own build target).  Instead we define a
// layout-compatible `PluginAbi` struct locally and reinterpret_cast
// the void* the plugin's entry symbol returns.  The fields and their
// order MUST match the `winampMediaLibraryPlugin` ABI exactly.
//
namespace {

struct PluginAbi {
    int            version;
    const char    *description;
    int          (*init)();
    void         (*quit)();
    INT_PTR      (*MessageProc)(int, INT_PTR, INT_PTR, INT_PTR);
    HWND           hwndWinampParent;
    HWND           hwndLibraryParent;
    HINSTANCE      hDllInstance;
    void          *service;  // api_service *
};

}  // anonymous

// Plugin-search directories.  Built-in build path is set at compile
// time via the QTAMP_BUILTIN_PLUGIN_DIR define; the user path is
// ~/.qtamp/plugins/, the conventional per-user plugin install
// location.
#ifndef QTAMP_BUILTIN_PLUGIN_DIR
#  define QTAMP_BUILTIN_PLUGIN_DIR ""
#endif

namespace {

QStringList pluginSearchDirs() {
    QStringList dirs;
    // Build-tree directory first — convenient for developers running
    // qtamp out of `build/`.  Set by the CMake parent project.
    if (*QTAMP_BUILTIN_PLUGIN_DIR) {
        dirs << QStringLiteral(QTAMP_BUILTIN_PLUGIN_DIR);
    }
    // User-installed plugins.
    dirs << (QDir::homePath() + QStringLiteral("/.qtamp/plugins"));
    return dirs;
}

// Open each `.so` in a search dir, look up `winampGetMediaLibraryPlugin`
// and `qtwasabi_null_api_service`, call init() with our nav HWND.
// Stores the dlopen handles in a static vector so they stay loaded
// for the process lifetime.
struct LoadedPlugin {
    QString name;
    void   *handle;
    PluginAbi *plugin;
};

std::vector<LoadedPlugin> &loadedPlugins() {
    static std::vector<LoadedPlugin> v;
    return v;
}

}  // anonymous

void loadBuiltinMlPlugins() {
    static std::atomic<bool> g_loaded{false};
    if (g_loaded.exchange(true)) return;

    HWND hLib = ensureLibraryWindow();

    for (const QString &dir : pluginSearchDirs()) {
        QDir d(dir);
        if (!d.exists()) continue;
        const QFileInfoList sos =
            d.entryInfoList({QStringLiteral("*.so")},
                              QDir::Files | QDir::Readable);
        for (const QFileInfo &fi : sos) {
            const QByteArray path = fi.absoluteFilePath().toUtf8();
            // RTLD_LAZY — defer symbol resolution to first call, so a
            // plugin that references but never calls an extra symbol
            // still loads.  RTLD_NOW would reject any plugin with an
            // undefined symbol even if it's never invoked, forcing us
            // to stub every cross-TU reference from the excluded files
            // (~30 for ml_playlists alone).
            void *h = dlopen(path.constData(), RTLD_LAZY);
            if (!h) {
                std::fprintf(stderr,
                    "[qtwasabi-ml] dlopen %s failed: %s\n",
                    fi.fileName().toUtf8().constData(), dlerror());
                continue;
            }
            using EntryFn = void *(*)();
            using NullSvcFn = void *(*)();
            auto entry =
                reinterpret_cast<EntryFn>(
                    dlsym(h, "winampGetMediaLibraryPlugin"));
            if (!entry) {
                std::fprintf(stderr,
                    "[qtwasabi-ml] %s: missing winampGetMediaLibraryPlugin\n",
                    fi.fileName().toUtf8().constData());
                dlclose(h);
                continue;
            }
            auto nullSvc =
                reinterpret_cast<NullSvcFn>(
                    dlsym(h, "qtwasabi_null_api_service"));
            auto *plugin = reinterpret_cast<PluginAbi *>(entry());
            if (!plugin) {
                std::fprintf(stderr,
                    "[qtwasabi-ml] %s: entry returned null\n",
                    fi.fileName().toUtf8().constData());
                dlclose(h);
                continue;
            }

            // Populate the host-supplied fields the plugin's init reads.
            plugin->hwndLibraryParent = hLib;
            plugin->hwndWinampParent  = nullptr;
            plugin->hDllInstance      = nullptr;
            plugin->service           = nullSvc ? nullSvc() : nullptr;

            int rc = plugin->init ? plugin->init() : -1;
            std::fprintf(stderr,
                "[qtwasabi-ml] %s init() = %d (%s)\n",
                fi.fileName().toUtf8().constData(), rc,
                rc == 0   ? "ML_INIT_SUCCESS"
                : rc == 1 ? "ML_INIT_FAILURE / WasabiApi_Initialize"
                : rc == 2 ? "no browserManager service"
                          : "unknown");
            std::fflush(stderr);

            loadedPlugins().push_back({fi.fileName(), h, plugin});
        }
    }
}

}  // namespace ml
}  // namespace qtWasabi
