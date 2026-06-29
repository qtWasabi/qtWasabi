// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// link-stubs.cpp — link-time placeholders for ml_playlists.
//
// Linker errors that surface from the plugin's translation units get
// added here using the same out-of-line-method pattern as
// ml_nowplaying's link-stubs.cpp.
//

#include <strsafe.h>
#include <windows.h>
#include <wtypes.h>
#include <objbase.h>

// view_playlistDialogProc + view_playlistsDialogProc — the playlist
// editor and list-window dialog procs.  Defined in the view TUs we
// deliberately excluded from the build (deep dependency cascade we
// don't have shims for).  The plugin references the addresses; stub
// them so the symbol table is satisfied.  Plain C++ linkage to match
// the declaration in the plugin header (no extern "C").
INT_PTR CALLBACK view_playlistDialogProc(HWND, UINT, WPARAM, LPARAM) {
    return FALSE;
}
INT_PTR CALLBACK view_playlistsDialogProc(HWND, UINT, WPARAM, LPARAM) {
    return FALSE;
}

// browseEnumProc — EnumChildWindows callback defined in one of the
// excluded view TUs and referenced by the current-playlist code.
BOOL CALLBACK browseEnumProc(HWND, LPARAM) {
    return TRUE;  // continue enumeration
}

// plstring_init/quit — initialize the playlist string-pool
// subsystem.  We don't compile that source; ml_playlists only calls
// plstring_init() once at Plugin_Init.  Stub to no-op.
void plstring_init() {}
void plstring_quit() {}

// `mediaLibrary` is a global instance of `MediaLibraryInterface`.
// ml_playlists references many of its methods; declare them all and
// provide harmless empty bodies.  The class layout (data members) is
// unimportant as long as we don't touch `this->...` — name mangling
// depends only on signature.
struct MLTREEITEMW;       // opaque
struct IDispatch;         // opaque
struct _COLOR24;          // opaque
typedef void (*BMPFILTERPROC)(const _COLOR24 *, const _COLOR24 *, _COLOR24 *);
class MediaLibraryInterface {
public:
    void *_opaque[64];

    int            AddTreeImage(int);
    int            AddTreeImage(int, int, BMPFILTERPROC);
    int            AddTreeImageBmp(int);
    void           AddTreeItem(MLTREEITEMW &);
    void           InsertTreeItem(MLTREEITEMW &);
    void           SetTreeItem(MLTREEITEMW &);
    void           RemoveTreeItem(long);
    void           SelectTreeItem(long);
    long           GetChildId(long);
    long           GetNextId(long);
    void           RenameTreeId(long, const wchar_t *);
    void           AddToSendTo(wchar_t *, long, long);
    void           BranchSendTo(long);
    void           AddToBranchSendTo(const wchar_t *, long, long);
    void           EndBranchSendTo(const wchar_t *, long);
    const wchar_t *GetIniDirectoryW();
    const wchar_t *GetWinampIni();
    void           BuildPath(const wchar_t *, wchar_t *, unsigned long);
    void           AddDispatch(wchar_t *, IDispatch *);
    int            EnqueueFile(const wchar_t *);
    int            PlayFile(const wchar_t *);
    int            GetFileInfo(const wchar_t *, wchar_t *, int, int *);
};
MediaLibraryInterface mediaLibrary;

int  MediaLibraryInterface::AddTreeImage(int)                      { return -1; }
int  MediaLibraryInterface::AddTreeImage(int, int, BMPFILTERPROC)  { return -1; }
int  MediaLibraryInterface::AddTreeImageBmp(int)                    { return -1; }
void MediaLibraryInterface::AddTreeItem(MLTREEITEMW &)             {}
void MediaLibraryInterface::InsertTreeItem(MLTREEITEMW &)          {}
void MediaLibraryInterface::SetTreeItem(MLTREEITEMW &)             {}
void MediaLibraryInterface::RemoveTreeItem(long)                   {}
void MediaLibraryInterface::SelectTreeItem(long)                   {}
long MediaLibraryInterface::GetChildId(long)                       { return 0; }
long MediaLibraryInterface::GetNextId(long)                        { return 0; }
void MediaLibraryInterface::RenameTreeId(long, const wchar_t *)    {}
void MediaLibraryInterface::AddToSendTo(wchar_t *, long, long)     {}
void MediaLibraryInterface::BranchSendTo(long)                     {}
void MediaLibraryInterface::AddToBranchSendTo(const wchar_t *, long, long) {}
void MediaLibraryInterface::EndBranchSendTo(const wchar_t *, long) {}
const wchar_t *MediaLibraryInterface::GetIniDirectoryW()           { return L""; }
const wchar_t *MediaLibraryInterface::GetWinampIni()               { return L""; }
void MediaLibraryInterface::BuildPath(const wchar_t *suffix,
                                       wchar_t *out, unsigned long n) {
    if (out && n > 0) {
        size_t i = 0;
        if (suffix) for (; i + 1 < n && suffix[i]; ++i) out[i] = suffix[i];
        out[i] = 0;
    }
}
void MediaLibraryInterface::AddDispatch(wchar_t *, IDispatch *)    {}
int  MediaLibraryInterface::EnqueueFile(const wchar_t *)           { return 0; }
int  MediaLibraryInterface::PlayFile   (const wchar_t *)           { return 0; }
int  MediaLibraryInterface::GetFileInfo(const wchar_t *, wchar_t *, int, int *) {
    return 0;
}

// ── Globals defined in excluded TUs ──────────────────────────────
// The view TUs are excluded from the build, but other plugin TUs
// reference these as `extern`.  Provide opaque storage in the plugin
// .so so dlopen resolves; calls into them WILL go through but won't
// drive behaviour until the excluded files come back online.

class Playlist {
public:
    void *_opaque[64];
};
Playlist  currentPlaylist;

HWND      activeHWND        = nullptr;
wchar_t   current_playing[4096] = {0};
int       cloud_avail       = 0;
HINSTANCE cloud_hinst       = nullptr;
int       cloudImage        = 0;
int       normalimage       = 0;
int       groupBtn          = 0;
int       customAllowed     = 0;
int       enqueuedef        = 0;
int       IPC_GET_CLOUD_HINST   = -1;
int       IPC_GET_CLOUD_ACTIVE  = -1;
int       IPC_LIBRARY_SENDTOMENU = -1;
// lastActiveID — defined in another plugin TU; don't redefine.
BOOL      we_are_drag_and_dropping = FALSE;

// playlist_list (list/vector of pl_entry pointers) from an excluded
// view TU.  Plugin code only iterates; opaque storage matching
// std::vector shape is enough to back accesses we'd never make.
void *playlist_list[4] = {nullptr, nullptr, nullptr, nullptr};

// C_Config — ml_playlists uses the wchar_t* variant (cf. ml_nowplaying's
// char* one).  Stub all referenced methods.
class C_Config {
public:
    C_Config(wchar_t *path);
    ~C_Config();
    int           ReadInt    (wchar_t *key, int def);
    void          WriteInt   (wchar_t *key, int value);
    wchar_t      *ReadString (wchar_t *key, wchar_t *def);
    void          WriteString(wchar_t *key, wchar_t *value);
};
C_Config::C_Config(wchar_t *) {}
C_Config::~C_Config() {}
int      C_Config::ReadInt    (wchar_t *, int def)         { return def; }
void     C_Config::WriteInt   (wchar_t *, int)             {}
wchar_t *C_Config::ReadString (wchar_t *, wchar_t *def)    { return def; }
void     C_Config::WriteString(wchar_t *, wchar_t *)       {}
