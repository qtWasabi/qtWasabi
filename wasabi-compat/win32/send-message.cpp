// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// send-message.cpp — SendMessage / PostMessage entry
// points + DefWindowProc / GetDlgItem / ShowWindow / GWL_*
// implementations.
//
// SendMessage routes synchronously through the HWND registry's
// `WindowObject::wndProc`.  Cross-thread sends should marshal
// through the GUI thread; we currently direct-call.  Once
// worker-thread sources (decode threadpool, indexer) exist, a
// Qt::BlockingQueuedConnection hop lands here.
//
// PostMessage is asynchronous in Win32; until we wire a real
// message queue we route it through SendMessage with no return
// value.  Adequate for boot-up sequences, but should defer onto a
// real queue before any code path uses PostMessage to defer
// reentrant work.
//

#include "win32/handle-registry.h"
#include "win32/winuser.h"
#include "win32/shlobj.h"   // IMalloc / SHFILEOPSTRUCTW

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>   // readlink for GetModuleFileName

namespace qtWasabi::wasabi_compat {

// Trace stub: gen_ml's WM_NOTIFY chain is deep enough that a
// printf-on-every-message is the only way to debug the early
// boot.  Toggle via QTWASABI_TRACE_WM=1.
namespace {
bool wmTraceEnabled() {
    static const bool on = [] {
        const char *v = ::getenv("QTWASABI_TRACE_WM");
        return v && v[0] == '1';
    }();
    return on;
}
}  // anonymous

}  // namespace qtWasabi::wasabi_compat

// The genex theme bitmap wa_dlg's WADlg_init fetches via WM_WA_IPC.
// Installed by qtwasabi_set_genskin_bitmap (from the ML host once the
// skin's colours are known); answered in SendMessageW below.
static HBITMAP g_genskinBitmap = nullptr;

extern "C" void qtwasabi_set_genskin_bitmap(HBITMAP genex) {
    g_genskinBitmap = genex;
}

extern "C" {

LRESULT WINAPI SendMessageW(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    using namespace qtWasabi::wasabi_compat;
    // wa_dlg's WADlg_init asks the "winamp" window for the genex theme
    // bitmap: WM_WA_IPC (= WM_USER) with lParam IPC_GET_GENSKINBITMAP
    // (= 503).  Answer globally — independent of which HWND it targets —
    // with the genex synthesised from the active skin's colours.
    if (msg == WM_USER && lp == 503)
        return reinterpret_cast<LRESULT>(g_genskinBitmap);
    if (!hwnd) return 0;
    WindowObject *w = lookupHandle<WindowObject>(hwnd);
    if (!w) {
        if (wmTraceEnabled())
            ::fprintf(stderr,
                "[wm] SendMessage to stale/invalid HWND %p msg=0x%04X\n",
                (void *)hwnd, msg);
        return 0;
    }
    if (wmTraceEnabled())
        ::fprintf(stderr, "[wm] %p msg=0x%04X wp=%lx lp=%lx\n",
            (void *)hwnd, msg, (long)wp, (long)lp);
    return w->wndProc(msg, wp, lp);
}

LRESULT WINAPI SendMessageA(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // ANSI variants share the same dispatch — the per-window
    // wndProc handles encoding translation for the few messages
    // that carry char buffers (WM_SETTEXT et al.).
    return SendMessageW(hwnd, msg, wp, lp);
}

LRESULT WINAPI SendDlgItemMessageW(HWND parent, int ctlId, UINT msg,
                                     WPARAM wp, LPARAM lp) {
    HWND child = GetDlgItem(parent, ctlId);
    if (!child) return 0;
    return SendMessageW(child, msg, wp, lp);
}

LRESULT WINAPI SendDlgItemMessageA(HWND parent, int ctlId, UINT msg,
                                     WPARAM wp, LPARAM lp) {
    return SendDlgItemMessageW(parent, ctlId, msg, wp, lp);
}

BOOL WINAPI PostMessageW(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // Route through SendMessage synchronously.  Replace with a
    // Qt-event-queue post when any real reentrancy-sensitive path
    // uses PostMessage.
    (void)SendMessageW(hwnd, msg, wp, lp);
    return TRUE;
}

BOOL WINAPI PostMessageA(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return PostMessageW(hwnd, msg, wp, lp);
}

LRESULT WINAPI DefWindowProcW(HWND, UINT, WPARAM, LPARAM) {
    // Win32's DefWindowProc handles nonclient-area behaviour the
    // app declined.  qtWasabi has no nonclient area inside the
    // skin canvas — the host skin paints everything — so the
    // canonical "no-op return 0" suffices.
    return 0;
}

LRESULT WINAPI DefWindowProcA(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    return DefWindowProcW(h, m, wp, lp);
}

LRESULT WINAPI CallWindowProcW(WNDPROC fn, HWND h, UINT m,
                                 WPARAM wp, LPARAM lp) {
    if (!fn) return 0;
    return fn(h, m, wp, lp);
}

LRESULT WINAPI CallWindowProcA(WNDPROC fn, HWND h, UINT m,
                                 WPARAM wp, LPARAM lp) {
    return CallWindowProcW(fn, h, m, wp, lp);
}

// ── Misc HWND helpers ──────────────────────────────────────────

HWND WINAPI GetDlgItem(HWND /*parent*/, int /*ctlId*/) {
    // The dialog template → child HWND map isn't wired yet.  Until
    // then return null; callers fall through to whatever empty
    // path they had.  The trace flag surfaces unexpected null
    // returns during gen_ml boot so they're debuggable.
    using namespace qtWasabi::wasabi_compat;
    if (wmTraceEnabled())
        ::fprintf(stderr, "[wm] GetDlgItem stub returns null\n");
    return nullptr;
}

BOOL WINAPI IsWindow(HWND hwnd) {
    using namespace qtWasabi::wasabi_compat;
    return lookupHandle<WindowObject>(hwnd) != nullptr ? TRUE : FALSE;
}

BOOL WINAPI IsWindowVisible(HWND /*hwnd*/) {
    // No client-side visibility state yet; assume visible for any
    // valid HWND.  The WindowObject will track a visible flag.
    return TRUE;
}

BOOL WINAPI IsWindowEnabled(HWND /*hwnd*/) {
    // No client-side enabled state yet; assume enabled (wa_dlg uses this
    // only to grey out disabled button text — non-critical).
    return TRUE;
}

BOOL WINAPI ShowWindow(HWND hwnd, int /*cmd*/) {
    using namespace qtWasabi::wasabi_compat;
    WindowObject *w = lookupHandle<WindowObject>(hwnd);
    if (!w) return FALSE;
    // TODO: track visible state per HWND; this is currently a
    // no-op that pretends the window was shown.
    return TRUE;
}

BOOL WINAPI DestroyWindow(HWND hwnd) {
    using namespace qtWasabi::wasabi_compat;
    if (!hwnd) return FALSE;
    unregisterHandle<WindowObject>(hwnd);
    return TRUE;
}

HWND WINAPI GetParent(HWND hwnd) {
    using namespace qtWasabi::wasabi_compat;
    WindowObject *w = lookupHandle<WindowObject>(hwnd);
    return w ? w->parent : nullptr;
}

HWND WINAPI SetParent(HWND hwnd, HWND newParent) {
    using namespace qtWasabi::wasabi_compat;
    WindowObject *w = lookupHandle<WindowObject>(hwnd);
    if (!w) return nullptr;
    HWND prev = w->parent;
    w->parent = newParent;
    return prev;
}

LONG_PTR WINAPI GetWindowLongPtrW(HWND hwnd, int index) {
    using namespace qtWasabi::wasabi_compat;
    WindowObject *w = lookupHandle<WindowObject>(hwnd);
    if (!w) return 0;
    // Map negative Win32 GWL_* indices to user_data slots.  Most
    // gen_ml code touches GWL_USERDATA (-21) and GWL_WNDPROC (-4).
    switch (index) {
        case GWL_USERDATA: return w->user_data[0];
        case GWL_WNDPROC:  return w->user_data[1];
        case GWL_HINSTANCE:return w->user_data[2];
        case GWL_ID:       return w->user_data[3];
        case GWL_STYLE:    return w->user_data[4];
        case GWL_EXSTYLE:  return w->user_data[5];
        default:           return 0;
    }
}

LONG_PTR WINAPI SetWindowLongPtrW(HWND hwnd, int index, LONG_PTR value) {
    using namespace qtWasabi::wasabi_compat;
    WindowObject *w = lookupHandle<WindowObject>(hwnd);
    if (!w) return 0;
    LONG_PTR prev = GetWindowLongPtrW(hwnd, index);
    switch (index) {
        case GWL_USERDATA: w->user_data[0] = value; break;
        case GWL_WNDPROC:  w->user_data[1] = value; break;
        case GWL_HINSTANCE:w->user_data[2] = value; break;
        case GWL_ID:       w->user_data[3] = value; break;
        case GWL_STYLE:    w->user_data[4] = value; break;
        case GWL_EXSTYLE:  w->user_data[5] = value; break;
        default:           break;
    }
    return prev;
}

LONG_PTR WINAPI GetWindowLongPtrA(HWND h, int i) { return GetWindowLongPtrW(h, i); }
LONG_PTR WINAPI SetWindowLongPtrA(HWND h, int i, LONG_PTR v) { return SetWindowLongPtrW(h, i, v); }

BOOL WINAPI GetClientRect(HWND hwnd, LPRECT rect) {
    using namespace qtWasabi::wasabi_compat;
    if (!rect) return FALSE;
    WindowObject *w = lookupHandle<WindowObject>(hwnd);
    if (!w) { rect->left = rect->top = rect->right = rect->bottom = 0; return FALSE; }
    // Client rect is the WindowObject's own rect anchored at (0,0).
    rect->left   = 0;
    rect->top    = 0;
    rect->right  = w->rect.right - w->rect.left;
    rect->bottom = w->rect.bottom - w->rect.top;
    return TRUE;
}

BOOL WINAPI GetWindowRect(HWND hwnd, LPRECT rect) {
    using namespace qtWasabi::wasabi_compat;
    if (!rect) return FALSE;
    WindowObject *w = lookupHandle<WindowObject>(hwnd);
    if (!w) { rect->left = rect->top = rect->right = rect->bottom = 0; return FALSE; }
    // Window rect is in screen coords on Win32; we don't have a
    // global screen-coord system, so we report the parent-relative
    // rect verbatim.  Callers that care (popup positioning, drag
    // start) get an offsettable point.
    *rect = w->rect;
    return TRUE;
}

BOOL WINAPI MoveWindow(HWND hwnd, int x, int y, int w_, int h_, BOOL /*redraw*/) {
    using namespace qtWasabi::wasabi_compat;
    WindowObject *w = lookupHandle<WindowObject>(hwnd);
    if (!w) return FALSE;
    w->rect.left   = x;
    w->rect.top    = y;
    w->rect.right  = x + w_;
    w->rect.bottom = y + h_;
    // TODO: fire WM_SIZE / WM_MOVE on the WindowObject so
    // gen_ml's layout code reflows.
    return TRUE;
}

// GetModuleFileName — return the executable's path (best-effort).
DWORD WINAPI GetModuleFileNameW(HINSTANCE /*hModule*/,
                                  LPWSTR lpFilename, DWORD nSize) {
    if (!lpFilename || nSize == 0) return 0;
    // Read /proc/self/exe on Linux for the canonical exe path.
    char buf[1024];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) { lpFilename[0] = 0; return 0; }
    buf[n] = 0;
    DWORD i = 0;
    while (i + 1 < nSize && buf[i] != 0) {
        lpFilename[i] = static_cast<wchar_t>(buf[i]);
        ++i;
    }
    lpFilename[i] = 0;
    return i;
}

DWORD WINAPI GetModuleFileNameA(HINSTANCE /*hModule*/,
                                  LPSTR lpFilename, DWORD nSize) {
    if (!lpFilename || nSize == 0) return 0;
    char buf[1024];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) { lpFilename[0] = 0; return 0; }
    buf[n] = 0;
    DWORD i = 0;
    while (i + 1 < nSize && buf[i] != 0) {
        lpFilename[i] = buf[i];
        ++i;
    }
    lpFilename[i] = 0;
    return i;
}

// Menu API stubs.  Real menu state will live in MenuObject backings
// once they're wired through the HWND registry.  Until then return
// TRUE/null so plugin code's branches don't error.
HMODULE WINAPI GetModuleHandleA(LPCSTR /*moduleName*/) { return nullptr; }
HMODULE WINAPI GetModuleHandleW(LPCWSTR /*moduleName*/) { return nullptr; }
BOOL    WINAPI EnableMenuItem(HMENU /*h*/, UINT /*id*/, UINT /*flags*/) { return TRUE; }
BOOL    WINAPI SetMenuDefaultItem(HMENU /*h*/, UINT /*id*/, UINT /*flags*/) { return TRUE; }
HMENU   WINAPI GetSubMenu(HMENU /*h*/, int /*pos*/) { return nullptr; }
HMENU   WINAPI CreatePopupMenu(void) { return nullptr; }
BOOL    WINAPI DestroyMenu(HMENU /*h*/) { return TRUE; }
BOOL    WINAPI AppendMenuW(HMENU /*h*/, UINT /*flags*/, UINT_PTR /*id*/, LPCWSTR /*text*/) { return TRUE; }
BOOL    WINAPI AppendMenuA(HMENU /*h*/, UINT /*flags*/, UINT_PTR /*id*/, LPCSTR /*text*/) { return TRUE; }
BOOL    WINAPI DeleteMenu(HMENU /*h*/, UINT /*pos*/, UINT /*flags*/) { return TRUE; }

// Window properties — null-stub.  Per-window state lives in
// WindowObject's user_data slots; the prop API is a distinct
// kv store gen_ml uses for plugin-bound private data.  Stubbing
// here means the plugin's GetProp returns null → its branches
// pick the "no bound data" path.
HANDLE WINAPI GetPropW(HWND, LPCWSTR) { return nullptr; }
HANDLE WINAPI GetPropA(HWND, LPCSTR)  { return nullptr; }
BOOL   WINAPI SetPropW(HWND, LPCWSTR, HANDLE) { return TRUE; }
BOOL   WINAPI SetPropA(HWND, LPCSTR, HANDLE)  { return TRUE; }
HANDLE WINAPI RemovePropW(HWND, LPCWSTR) { return nullptr; }
HANDLE WINAPI RemovePropA(HWND, LPCSTR)  { return nullptr; }

// Dynamic loading — we don't ship a DLL registry.
FARPROC WINAPI GetProcAddress(HMODULE, LPCSTR) { return nullptr; }
HMODULE WINAPI LoadLibraryW(LPCWSTR) { return nullptr; }
HMODULE WINAPI LoadLibraryA(LPCSTR)  { return nullptr; }
BOOL    WINAPI FreeLibrary(HMODULE)  { return TRUE; }

// Window-coord mapping — stub.
int WINAPI MapWindowPoints(HWND, HWND, LPPOINT, UINT) { return 0; }

// GetClassName — stub returning empty string + 0.
int WINAPI GetClassNameA(HWND, LPSTR buf, int n) {
    if (buf && n > 0) buf[0] = 0;
    return 0;
}
int WINAPI GetClassNameW(HWND, LPWSTR buf, int n) {
    if (buf && n > 0) buf[0] = 0;
    return 0;
}

// ── shlobj.h shell-namespace stubs ─────────────────────────────
// All return "user cancelled / empty" so callers' null-checks bail
// at the right places.  ml_playlists's Browse-for-folder dialog
// won't function until a QFileDialog path is plumbed through.

// Bodies pulled in via shlobj.h.
LPITEMIDLIST WINAPI SHBrowseForFolderW(LPBROWSEINFOW) { return nullptr; }
LPITEMIDLIST WINAPI SHBrowseForFolder (LPBROWSEINFO)  { return nullptr; }
BOOL WINAPI SHGetPathFromIDListW(LPCITEMIDLIST, LPWSTR buf) {
    if (buf) buf[0] = 0;
    return FALSE;
}
BOOL WINAPI SHGetPathFromIDList(LPCITEMIDLIST, LPSTR buf) {
    if (buf) buf[0] = 0;
    return FALSE;
}
HRESULT WINAPI SHGetSpecialFolderLocation(HWND, int,
                                            LPITEMIDLIST *ppidl) {
    if (ppidl) *ppidl = nullptr;
    return (HRESULT)0x80004005L;  // E_FAIL
}
HRESULT WINAPI SHGetFolderPathW(HWND, int, HANDLE, DWORD,
                                  LPWSTR buf) {
    if (buf) buf[0] = 0;
    return (HRESULT)0x80004005L;  // E_FAIL
}
HRESULT WINAPI SHGetFolderPathA(HWND, int, HANDLE, DWORD,
                                  LPSTR buf) {
    if (buf) buf[0] = 0;
    return (HRESULT)0x80004005L;
}
void WINAPI CoTaskMemFree(void *) {}
DWORD WINAPI CommDlgExtendedError(void) { return 0; }

// _wsplitpath / _splitpath — manual path decompose.
void _wsplitpath(const wchar_t *path, wchar_t *drive, wchar_t *dir,
                  wchar_t *fname, wchar_t *ext) {
    if (drive) drive[0] = 0;
    if (dir)   dir[0]   = 0;
    if (fname) fname[0] = 0;
    if (ext)   ext[0]   = 0;
    if (!path) return;
    const wchar_t *last_sep = nullptr;
    const wchar_t *last_dot = nullptr;
    for (const wchar_t *p = path; *p; ++p) {
        if (*p == L'/' || *p == L'\\') { last_sep = p; last_dot = nullptr; }
        else if (*p == L'.') last_dot = p;
    }
    if (dir && last_sep) {
        size_t n = (size_t)(last_sep - path) + 1;
        for (size_t i = 0; i < n; ++i) dir[i] = path[i];
        dir[n] = 0;
    }
    const wchar_t *name_start = last_sep ? last_sep + 1 : path;
    const wchar_t *name_end   = last_dot ? last_dot : (path + wcslen(path));
    if (fname) {
        size_t n = (size_t)(name_end - name_start);
        for (size_t i = 0; i < n; ++i) fname[i] = name_start[i];
        fname[n] = 0;
    }
    if (ext && last_dot) {
        size_t i = 0;
        for (const wchar_t *p = last_dot; *p; ++p, ++i) ext[i] = *p;
        ext[i] = 0;
    }
}
void _splitpath(const char *path, char *drive, char *dir,
                 char *fname, char *ext) {
    if (drive) drive[0] = 0;
    if (dir)   dir[0]   = 0;
    if (fname) fname[0] = 0;
    if (ext)   ext[0]   = 0;
    if (!path) return;
    const char *last_sep = nullptr;
    const char *last_dot = nullptr;
    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') { last_sep = p; last_dot = nullptr; }
        else if (*p == '.') last_dot = p;
    }
    if (dir && last_sep) {
        size_t n = (size_t)(last_sep - path) + 1;
        std::memcpy(dir, path, n); dir[n] = 0;
    }
    const char *name_start = last_sep ? last_sep + 1 : path;
    const char *name_end   = last_dot ? last_dot : (path + std::strlen(path));
    if (fname) {
        size_t n = (size_t)(name_end - name_start);
        std::memcpy(fname, name_start, n); fname[n] = 0;
    }
    if (ext && last_dot) std::strcpy(ext, last_dot);
}
void _wmakepath(wchar_t *path, const wchar_t *, const wchar_t *dir,
                 const wchar_t *fname, const wchar_t *ext) {
    if (!path) return;
    size_t pos = 0;
    if (dir)   { for (size_t i = 0; dir[i];   ++i, ++pos) path[pos] = dir[i]; }
    if (fname) { for (size_t i = 0; fname[i]; ++i, ++pos) path[pos] = fname[i]; }
    if (ext)   { for (size_t i = 0; ext[i];   ++i, ++pos) path[pos] = ext[i]; }
    path[pos] = 0;
}
errno_t _wsplitpath_s(const wchar_t *path,
                        wchar_t *drive, size_t,
                        wchar_t *dir,   size_t,
                        wchar_t *fname, size_t,
                        wchar_t *ext,   size_t) {
    _wsplitpath(path, drive, dir, fname, ext);
    return 0;
}

// Canonical COM IIDs — exported from libqtwasabi so every plugin
// .so resolves them at dlopen-time without needing per-plugin
// storage.  Real Win32 ships these in OLE32.
extern "C" const IID IID_IUnknown    = {0x00000000, 0x0000, 0x0000,
    {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
extern "C" const IID IID_IDispatch   = {0x00020400, 0x0000, 0x0000,
    {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
extern "C" const IID IID_IDataObject = {0x0000010E, 0x0000, 0x0000,
    {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

// IMalloc + SHGetMalloc stubs.  Pulled in via shlobj.h.
static void *qtwa_malloc_alloc       (struct IMalloc *, SIZE_T n) {
    return std::malloc(n);
}
static void *qtwa_malloc_realloc     (struct IMalloc *, void *p, SIZE_T n) {
    return std::realloc(p, n);
}
static void  qtwa_malloc_free        (struct IMalloc *, void *p) {
    std::free(p);
}
static SIZE_T qtwa_malloc_getsize    (struct IMalloc *, void *) { return 0; }
static int   qtwa_malloc_didalloc    (struct IMalloc *, void *) { return 1; }
static void  qtwa_malloc_heapminimize(struct IMalloc *) {}
static IMalloc qtwa_imalloc_inst = {
    qtwa_malloc_alloc,
    qtwa_malloc_realloc,
    qtwa_malloc_free,
    qtwa_malloc_getsize,
    qtwa_malloc_didalloc,
    qtwa_malloc_heapminimize
};
HRESULT WINAPI SHGetMalloc(struct IMalloc **ppMalloc) {
    if (!ppMalloc) return (HRESULT)0x80004003L;  // E_POINTER
    *ppMalloc = &qtwa_imalloc_inst;
    return 0;
}
int WINAPI SHFileOperationW(LPSHFILEOPSTRUCTW) { return 0; }
int WINAPI SHFileOperationA(void *) { return 0; }

// ── Dialog API stubs (ml_playlists) ─────────────────────────
int  WINAPI MessageBoxW(HWND, LPCWSTR, LPCWSTR, UINT) { return 1; }  // IDOK
int  WINAPI MessageBoxA(HWND, LPCSTR,  LPCSTR,  UINT) { return 1; }
BOOL WINAPI EndDialog(HWND, INT_PTR) { return TRUE; }
BOOL WINAPI IsDlgButtonChecked(HWND, int) { return 0; }
BOOL WINAPI CheckDlgButton(HWND, int, UINT) { return TRUE; }
BOOL WINAPI CheckRadioButton(HWND, int, int, int) { return TRUE; }
BOOL WINAPI EnableWindow(HWND, BOOL) { return TRUE; }
BOOL WINAPI SetWindowTextW(HWND, LPCWSTR) { return TRUE; }
BOOL WINAPI SetWindowTextA(HWND, LPCSTR)  { return TRUE; }
int  WINAPI GetWindowTextW(HWND, LPWSTR buf, int) {
    if (buf) buf[0] = 0;
    return 0;
}
int  WINAPI GetWindowTextA(HWND, LPSTR buf, int) {
    if (buf) buf[0] = 0;
    return 0;
}
HWND WINAPI FindWindowExW(HWND, HWND, LPCWSTR, LPCWSTR) { return nullptr; }
HWND WINAPI FindWindowExA(HWND, HWND, LPCSTR,  LPCSTR)  { return nullptr; }
HWND WINAPI FindWindowW  (LPCWSTR, LPCWSTR) { return nullptr; }
HWND WINAPI FindWindowA  (LPCSTR,  LPCSTR)  { return nullptr; }
BOOL WINAPI EnumChildWindows(HWND, BOOL (CALLBACK *)(HWND, LPARAM), LPARAM) {
    return TRUE;
}
BOOL WINAPI SetWindowPos(HWND, HWND, int, int, int, int, UINT) { return TRUE; }
BOOL WINAPI ScreenToClient(HWND, LPPOINT) { return TRUE; }
BOOL WINAPI ClientToScreen(HWND, LPPOINT) { return TRUE; }
BOOL WINAPI InvalidateRect(HWND, const RECT *, BOOL) { return TRUE; }
BOOL WINAPI UpdateWindow  (HWND) { return TRUE; }
HWND WINAPI SetFocus      (HWND) { return nullptr; }
HWND WINAPI GetFocus      (void) { return nullptr; }
UINT_PTR WINAPI SetTimer  (HWND, UINT_PTR id, UINT, void *) { return id; }
BOOL WINAPI KillTimer     (HWND, UINT_PTR) { return TRUE; }
DWORD WINAPI GetTickCount (void) {
    return static_cast<DWORD>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

// ── Menu API extension ──────────────────────────────────────
HMENU WINAPI CreateMenu(void) { return nullptr; }
BOOL  WINAPI InsertMenuItemW(HMENU, UINT, BOOL, void *) { return TRUE; }
BOOL  WINAPI InsertMenuW    (HMENU, UINT, UINT, UINT_PTR, LPCWSTR) { return TRUE; }
BOOL  WINAPI InsertMenuA    (HMENU, UINT, UINT, UINT_PTR, LPCSTR)  { return TRUE; }
BOOL  WINAPI RemoveMenu     (HMENU, UINT, UINT) { return TRUE; }
BOOL  WINAPI CheckMenuItem  (HMENU, UINT, UINT) { return TRUE; }
BOOL  WINAPI CheckMenuRadioItem(HMENU, UINT, UINT, UINT, UINT) { return TRUE; }
BOOL  WINAPI GetMenuItemInfoW(HMENU, UINT, BOOL, void *) { return FALSE; }
BOOL  WINAPI SetMenuItemInfoW(HMENU, UINT, BOOL, void *) { return TRUE; }
int   WINAPI GetMenuItemCount(HMENU) { return 0; }
UINT  WINAPI GetMenuItemID   (HMENU, int) { return 0; }
HMENU WINAPI LoadMenuW       (HINSTANCE, LPCWSTR) { return nullptr; }

// ── Memory API ──────────────────────────────────────────────
// Use plain malloc/free under the hood — kernel32's HGLOBAL
// abstraction is unnecessary; Win32 also implements GlobalAlloc
// as malloc on modern Windows.
HGLOBAL WINAPI GlobalAlloc(UINT flags, SIZE_T n) {
    void *p = std::malloc(n ? n : 1);
    if (p && (flags & 0x40 /* GMEM_ZEROINIT */)) std::memset(p, 0, n);
    return reinterpret_cast<HGLOBAL>(p);
}
HGLOBAL WINAPI GlobalFree(HGLOBAL h) {
    std::free(reinterpret_cast<void *>(h));
    return nullptr;
}
HGLOBAL WINAPI GlobalReAlloc(HGLOBAL h, SIZE_T n, UINT) {
    return reinterpret_cast<HGLOBAL>(
        std::realloc(reinterpret_cast<void *>(h), n));
}
LPVOID WINAPI GlobalLock  (HGLOBAL h) { return reinterpret_cast<void *>(h); }
BOOL   WINAPI GlobalUnlock(HGLOBAL)   { return TRUE; }
SIZE_T WINAPI GlobalSize  (HGLOBAL)   { return 0; }

// ── File I/O ────────────────────────────────────────────────
// Implement against POSIX open/close — gives the plugin a real
// file handle so it can actually read/write playlists.  The HANDLE
// is just the POSIX fd cast through void*.
HANDLE WINAPI CreateFileW(LPCWSTR path, DWORD access, DWORD,
                            void *, DWORD disposition, DWORD, HANDLE) {
    if (!path) return INVALID_HANDLE_VALUE;
    char narrow[4096] = {0};
    for (size_t i = 0; i < sizeof(narrow) - 1 && path[i]; ++i)
        narrow[i] = static_cast<char>(path[i] & 0xff);
    int flags = 0;
    if ((access & 0x80000000U) && (access & 0x40000000U))
        flags = O_RDWR;
    else if (access & 0x80000000U)  flags = O_RDONLY;
    else if (access & 0x40000000U)  flags = O_WRONLY;
    if (disposition == 1 /*CREATE_NEW*/)    flags |= O_CREAT | O_EXCL;
    if (disposition == 2 /*CREATE_ALWAYS*/) flags |= O_CREAT | O_TRUNC;
    if (disposition == 4 /*OPEN_ALWAYS*/)   flags |= O_CREAT;
    int fd = ::open(narrow, flags, 0644);
    if (fd < 0) return INVALID_HANDLE_VALUE;
    return reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd));
}
HANDLE WINAPI CreateFileA(LPCSTR path, DWORD access, DWORD share,
                            void *sec, DWORD disposition, DWORD flags,
                            HANDLE tmpl) {
    wchar_t wpath[4096] = {0};
    if (path) {
        for (size_t i = 0; i < sizeof(wpath)/sizeof(wpath[0]) - 1
                          && path[i]; ++i)
            wpath[i] = static_cast<wchar_t>(
                static_cast<unsigned char>(path[i]));
    }
    return CreateFileW(wpath, access, share, sec, disposition, flags, tmpl);
}
BOOL WINAPI CloseHandle(HANDLE h) {
    if (h == INVALID_HANDLE_VALUE || !h) return FALSE;
    ::close(static_cast<int>(reinterpret_cast<intptr_t>(h)));
    return TRUE;
}
BOOL WINAPI CopyFileW(LPCWSTR, LPCWSTR, BOOL) { return FALSE; }
BOOL WINAPI CopyFileA(LPCSTR,  LPCSTR,  BOOL) { return FALSE; }
BOOL WINAPI MoveFileW(LPCWSTR, LPCWSTR) { return FALSE; }
BOOL WINAPI DeleteFileW(LPCWSTR path) {
    if (!path) return FALSE;
    char narrow[4096] = {0};
    for (size_t i = 0; i < sizeof(narrow) - 1 && path[i]; ++i)
        narrow[i] = static_cast<char>(path[i] & 0xff);
    return ::unlink(narrow) == 0 ? TRUE : FALSE;
}
BOOL WINAPI DeleteFileA(LPCSTR path) {
    return path && ::unlink(path) == 0 ? TRUE : FALSE;
}
UINT WINAPI GetTempFileNameW(LPCWSTR, LPCWSTR, UINT, LPWSTR buf) {
    if (buf) {
        const wchar_t *tmpl = L"/tmp/qtwasabi-XXXXXX";
        size_t i = 0;
        for (; tmpl[i]; ++i) buf[i] = tmpl[i];
        buf[i] = 0;
    }
    return 1;
}
DWORD WINAPI GetTempPathW(DWORD n, LPWSTR buf) {
    const wchar_t *p = L"/tmp/";
    DWORD i = 0;
    if (buf && n > 0) {
        for (; i + 1 < n && p[i]; ++i) buf[i] = p[i];
        buf[i] = 0;
    }
    return i;
}
DWORD WINAPI GetFileSize(HANDLE h, LPDWORD high) {
    if (h == INVALID_HANDLE_VALUE || !h) return 0;
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(h));
    off_t cur = ::lseek(fd, 0, SEEK_CUR);
    off_t end = ::lseek(fd, 0, SEEK_END);
    ::lseek(fd, cur, SEEK_SET);
    if (high) *high = static_cast<DWORD>((end >> 32) & 0xFFFFFFFFu);
    return static_cast<DWORD>(end & 0xFFFFFFFFu);
}
BOOL WINAPI ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD outN, void *) {
    if (h == INVALID_HANDLE_VALUE || !h) return FALSE;
    ssize_t r = ::read(
        static_cast<int>(reinterpret_cast<intptr_t>(h)), buf, n);
    if (r < 0) return FALSE;
    if (outN) *outN = static_cast<DWORD>(r);
    return TRUE;
}
BOOL WINAPI WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD outN, void *) {
    if (h == INVALID_HANDLE_VALUE || !h) return FALSE;
    ssize_t w = ::write(
        static_cast<int>(reinterpret_cast<intptr_t>(h)), buf, n);
    if (w < 0) return FALSE;
    if (outN) *outN = static_cast<DWORD>(w);
    return TRUE;
}
DWORD WINAPI SetFilePointer(HANDLE h, LONG d, PLONG, DWORD whence) {
    if (h == INVALID_HANDLE_VALUE || !h) return (DWORD)-1;
    return static_cast<DWORD>(::lseek(
        static_cast<int>(reinterpret_cast<intptr_t>(h)),
        d, static_cast<int>(whence)));
}
DWORD WINAPI GetFileAttributesW(LPCWSTR path) {
    if (!path) return INVALID_FILE_ATTRIBUTES;
    char narrow[4096] = {0};
    for (size_t i = 0; i < sizeof(narrow) - 1 && path[i]; ++i)
        narrow[i] = static_cast<char>(path[i] & 0xff);
    struct stat st;
    if (::stat(narrow, &st) != 0) return INVALID_FILE_ATTRIBUTES;
    DWORD attrs = 0;
    if (S_ISDIR(st.st_mode)) attrs |= FILE_ATTRIBUTE_DIRECTORY;
    if (!attrs) attrs = FILE_ATTRIBUTE_NORMAL;
    return attrs;
}
BOOL WINAPI SetFileAttributesW(LPCWSTR, DWORD) { return TRUE; }

// ── Profile / INI API ───────────────────────────────────────
// Real Win32 keeps a per-process cache of parsed INI files; we
// don't.  Return defaults / no-op so callers' fallback paths run.
UINT WINAPI GetPrivateProfileIntA(LPCSTR, LPCSTR, INT def, LPCSTR) {
    return static_cast<UINT>(def);
}
UINT WINAPI GetPrivateProfileIntW(LPCWSTR, LPCWSTR, INT def, LPCWSTR) {
    return static_cast<UINT>(def);
}
DWORD WINAPI GetPrivateProfileStringA(LPCSTR, LPCSTR, LPCSTR def,
                                        LPSTR out, DWORD sz, LPCSTR) {
    if (out && sz > 0) {
        out[0] = 0;
        if (def) {
            size_t i = 0;
            for (; i + 1 < sz && def[i]; ++i) out[i] = def[i];
            out[i] = 0;
            return static_cast<DWORD>(i);
        }
    }
    return 0;
}
DWORD WINAPI GetPrivateProfileStringW(LPCWSTR, LPCWSTR, LPCWSTR def,
                                        LPWSTR out, DWORD sz, LPCWSTR) {
    if (out && sz > 0) {
        out[0] = 0;
        if (def) {
            size_t i = 0;
            for (; i + 1 < sz && def[i]; ++i) out[i] = def[i];
            out[i] = 0;
            return static_cast<DWORD>(i);
        }
    }
    return 0;
}
BOOL WINAPI WritePrivateProfileStringA(LPCSTR, LPCSTR, LPCSTR, LPCSTR) {
    return TRUE;
}
BOOL WINAPI WritePrivateProfileStringW(LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR) {
    return TRUE;
}

// ── Common-dialog OPENFILENAMEW stubs ───────────────────────
BOOL WINAPI GetOpenFileNameW(LPOPENFILENAMEW) { return FALSE; }
BOOL WINAPI GetSaveFileNameW(LPOPENFILENAMEW) { return FALSE; }

// ── Current-directory stubs ─────────────────────────────────
// Pass through to POSIX getcwd / chdir.
DWORD WINAPI GetCurrentDirectoryW(DWORD n, LPWSTR buf) {
    char tmp[4096] = {0};
    if (!::getcwd(tmp, sizeof(tmp))) return 0;
    DWORD i = 0;
    if (buf && n > 0) {
        for (; i + 1 < n && tmp[i]; ++i)
            buf[i] = static_cast<wchar_t>(static_cast<unsigned char>(tmp[i]));
        buf[i] = 0;
    } else {
        for (; tmp[i]; ++i) {}
    }
    return i;
}
DWORD WINAPI GetCurrentDirectoryA(DWORD n, LPSTR buf) {
    if (!buf || n == 0) {
        char tmp[4096] = {0};
        if (!::getcwd(tmp, sizeof(tmp))) return 0;
        DWORD i = 0;
        for (; tmp[i]; ++i) {}
        return i;
    }
    return ::getcwd(buf, n) ? static_cast<DWORD>(std::strlen(buf)) : 0;
}
BOOL WINAPI SetCurrentDirectoryW(LPCWSTR path) {
    if (!path) return FALSE;
    char narrow[4096] = {0};
    for (size_t i = 0; i < sizeof(narrow) - 1 && path[i]; ++i)
        narrow[i] = static_cast<char>(path[i] & 0xff);
    return ::chdir(narrow) == 0 ? TRUE : FALSE;
}
BOOL WINAPI SetCurrentDirectoryA(LPCSTR path) {
    return path && ::chdir(path) == 0 ? TRUE : FALSE;
}

// ── Cursor / Icon loaders — no-op stubs ─────────────────────
HCURSOR WINAPI LoadCursorW(HINSTANCE, LPCWSTR) { return nullptr; }
HCURSOR WINAPI LoadCursorA(HINSTANCE, LPCSTR)  { return nullptr; }
HICON   WINAPI LoadIconW  (HINSTANCE, LPCWSTR) { return nullptr; }

// ── Dialog-item text stubs ──────────────────────────────────
UINT WINAPI GetDlgItemTextW(HWND, int, LPWSTR buf, int) {
    if (buf) buf[0] = 0;
    return 0;
}
UINT WINAPI GetDlgItemTextA(HWND, int, LPSTR buf, int) {
    if (buf) buf[0] = 0;
    return 0;
}
BOOL WINAPI SetDlgItemTextW(HWND, int, LPCWSTR) { return TRUE; }
BOOL WINAPI SetDlgItemTextA(HWND, int, LPCSTR)  { return TRUE; }
UINT WINAPI GetDlgItemInt(HWND, int, BOOL *ok, BOOL) {
    if (ok) *ok = FALSE;
    return 0;
}
BOOL WINAPI SetDlgItemInt(HWND, int, UINT, BOOL) { return TRUE; }

// Key-state stubs — return 0 ("not pressed").
SHORT WINAPI GetAsyncKeyState(int) { return 0; }
SHORT WINAPI GetKeyState(int) { return 0; }

// Mouse-capture stubs.
HWND WINAPI SetCapture(HWND)      { return nullptr; }
BOOL WINAPI ReleaseCapture(void)  { return TRUE; }
HWND WINAPI GetCapture(void)      { return nullptr; }
BOOL WINAPI GetCursorPos(LPPOINT p) {
    if (p) { p->x = 0; p->y = 0; }
    return TRUE;
}
BOOL WINAPI SetCursorPos(int, int) { return TRUE; }

// Rect helpers — proper implementations because plugin code branches
// on the results.
BOOL WINAPI CopyRect(LPRECT dst, const RECT *src) {
    if (!dst || !src) return FALSE;
    *dst = *src;
    return TRUE;
}
BOOL WINAPI EqualRect(const RECT *a, const RECT *b) {
    if (!a || !b) return FALSE;
    return a->left == b->left && a->top == b->top
        && a->right == b->right && a->bottom == b->bottom;
}
BOOL WINAPI InflateRect(LPRECT r, int dx, int dy) {
    if (!r) return FALSE;
    r->left -= dx; r->top -= dy; r->right += dx; r->bottom += dy;
    return TRUE;
}
BOOL WINAPI IntersectRect(LPRECT dst, const RECT *a, const RECT *b) {
    if (!dst || !a || !b) return FALSE;
    dst->left   = a->left   > b->left   ? a->left   : b->left;
    dst->top    = a->top    > b->top    ? a->top    : b->top;
    dst->right  = a->right  < b->right  ? a->right  : b->right;
    dst->bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
    return (dst->left < dst->right && dst->top < dst->bottom);
}
BOOL WINAPI OffsetRect(LPRECT r, int dx, int dy) {
    if (!r) return FALSE;
    r->left += dx; r->top += dy; r->right += dx; r->bottom += dy;
    return TRUE;
}
BOOL WINAPI PtInRect(const RECT *r, POINT p) {
    if (!r) return FALSE;
    return p.x >= r->left && p.x < r->right
        && p.y >= r->top  && p.y < r->bottom;
}
BOOL WINAPI SetRect(LPRECT r, int l, int t, int rr, int b) {
    if (!r) return FALSE;
    r->left = l; r->top = t; r->right = rr; r->bottom = b;
    return TRUE;
}
BOOL WINAPI SetRectEmpty(LPRECT r) {
    if (!r) return FALSE;
    r->left = r->top = r->right = r->bottom = 0;
    return TRUE;
}
BOOL WINAPI UnionRect(LPRECT dst, const RECT *a, const RECT *b) {
    if (!dst || !a || !b) return FALSE;
    dst->left   = a->left   < b->left   ? a->left   : b->left;
    dst->top    = a->top    < b->top    ? a->top    : b->top;
    dst->right  = a->right  > b->right  ? a->right  : b->right;
    dst->bottom = a->bottom > b->bottom ? a->bottom : b->bottom;
    return (dst->left < dst->right && dst->top < dst->bottom);
}
BOOL WINAPI DrawFocusRect(HDC, const RECT *) { return TRUE; }

// Sleep, PeekMessage, etc.
void WINAPI Sleep(DWORD ms) { ::usleep(ms * 1000); }
BOOL WINAPI PeekMessageW(LPMSG, HWND, UINT, UINT, UINT) { return FALSE; }
BOOL WINAPI PeekMessageA(LPMSG, HWND, UINT, UINT, UINT) { return FALSE; }
BOOL WINAPI GetMessageW (LPMSG, HWND, UINT, UINT) { return FALSE; }
LONG WINAPI DispatchMessageW(const MSG *) { return 0; }
BOOL WINAPI TranslateMessage(const MSG *) { return TRUE; }

// GetShortPathNameW — pass-through.
DWORD WINAPI GetShortPathNameW(LPCWSTR longPath, LPWSTR shortPath, DWORD sz) {
    if (!longPath || !shortPath || sz == 0) return 0;
    DWORD i = 0;
    for (; i + 1 < sz && longPath[i]; ++i) shortPath[i] = longPath[i];
    shortPath[i] = 0;
    return i;
}

// Accelerator-table stubs.
HACCEL WINAPI CreateAcceleratorTableW(LPACCEL, int)        { return nullptr; }
int    WINAPI CopyAcceleratorTableW  (HACCEL, LPACCEL, int) { return 0; }
int    WINAPI CopyAcceleratorTable   (HACCEL, LPACCEL, int) { return 0; }
BOOL   WINAPI DestroyAcceleratorTable(HACCEL)              { return TRUE; }
int    WINAPI TranslateAcceleratorW  (HWND, HACCEL, LPMSG) { return 0; }

// shellapi.h stubs.
HINSTANCE WINAPI ShellExecuteW(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, INT) {
    return nullptr;
}
HINSTANCE WINAPI ShellExecuteA(HWND, LPCSTR,  LPCSTR,  LPCSTR,  LPCSTR,  INT) {
    return nullptr;
}
BOOL WINAPI DragAcceptFiles(HWND, BOOL) { return TRUE; }
void WINAPI DragFinish     (HANDLE)     {}
UINT WINAPI DragQueryFileW (HANDLE, UINT, LPWSTR buf, UINT) {
    if (buf) buf[0] = 0;
    return 0;
}
UINT WINAPI DragQueryFileA (HANDLE, UINT, LPSTR buf, UINT) {
    if (buf) buf[0] = 0;
    return 0;
}

// FindFirstFile / FindNextFile / FindClose — null stubs.
HANDLE WINAPI FindFirstFileW(LPCWSTR, void *) { return INVALID_HANDLE_VALUE; }
HANDLE WINAPI FindFirstFileA(LPCSTR,  void *) { return INVALID_HANDLE_VALUE; }
BOOL   WINAPI FindNextFileW (HANDLE, void *) { return FALSE; }
BOOL   WINAPI FindNextFileA (HANDLE, void *) { return FALSE; }
BOOL   WINAPI FindClose     (HANDLE) { return TRUE; }

}  // extern "C"
