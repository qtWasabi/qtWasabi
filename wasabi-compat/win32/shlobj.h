// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#ifndef _SHLOBJ_H_INCLUDED_
#define _SHLOBJ_H_INCLUDED_
//
// shlobj.h — Win32 Shell-Object surface used by ml_playlists for
// the "Browse for folder" dialog.  On Linux/macOS there is no
// equivalent native dialog backing this, so we provide link-time
// stubs so the source compiles, with TODOs for a Qt-backed
// QFileDialog path.
//

#include "basetsd.h"
#include "windef.h"
#include "winuser.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── ITEMIDLIST + helpers ─────────────────────────────────────
// Real Win32 uses a packed variable-length list of SHITEMID
// records to identify shell namespace objects.  We never
// actually walk these; passing them around as opaque pointers
// is enough for ml_playlists's `SHBrowseForFolderW` callers.
typedef struct _ITEMIDLIST {
    char _opaque[1];
} ITEMIDLIST, *LPITEMIDLIST, *PIDLIST_ABSOLUTE;
typedef const ITEMIDLIST *LPCITEMIDLIST, *PCIDLIST_ABSOLUTE;

// ── BROWSEINFOW (folder picker setup) ─────────────────────────
typedef int (CALLBACK *BFFCALLBACK)(HWND hwnd, UINT uMsg,
                                      LPARAM lParam, LPARAM lpData);

typedef struct _browseinfoW {
    HWND          hwndOwner;
    LPCITEMIDLIST pidlRoot;
    LPWSTR        pszDisplayName;
    LPCWSTR       lpszTitle;
    UINT          ulFlags;
    BFFCALLBACK   lpfn;
    LPARAM        lParam;
    int           iImage;
} BROWSEINFOW, *PBROWSEINFOW, *LPBROWSEINFOW;
typedef BROWSEINFOW BROWSEINFO;
typedef LPBROWSEINFOW LPBROWSEINFO;

// BFFM_* callback notifications.
#define BFFM_INITIALIZED         1
#define BFFM_SELCHANGED          2
#define BFFM_VALIDATEFAILEDA     3
#define BFFM_VALIDATEFAILEDW     4
#define BFFM_IUNKNOWN            5
#define BFFM_SETSTATUSTEXTA      0x0464
#define BFFM_SETSTATUSTEXTW      0x0468
#define BFFM_SETSELECTIONA       0x0466
#define BFFM_SETSELECTIONW       0x0467
#define BFFM_ENABLEOK            0x0465
#define BFFM_SETOKTEXT           0x0469
#define BFFM_SETEXPANDED         0x046A

// BIF_* flags.
#define BIF_RETURNONLYFSDIRS    0x00000001
#define BIF_DONTGOBELOWDOMAIN   0x00000002
#define BIF_STATUSTEXT          0x00000004
#define BIF_RETURNFSANCESTORS   0x00000008
#define BIF_EDITBOX             0x00000010
#define BIF_VALIDATE            0x00000020
#define BIF_NEWDIALOGSTYLE      0x00000040
#define BIF_USENEWUI            (BIF_EDITBOX | BIF_NEWDIALOGSTYLE)
#define BIF_BROWSEINCLUDEURLS   0x00000080
#define BIF_BROWSEFORCOMPUTER   0x00001000

// CSIDL_* known-folder ids — only the ones ml_playlists uses.
#define CSIDL_PERSONAL          0x0005
#define CSIDL_MYMUSIC           0x000D
#define CSIDL_APPDATA           0x001A
#define CSIDL_LOCAL_APPDATA     0x001C
#define CSIDL_PROGRAM_FILES     0x0026
#define CSIDL_FLAG_CREATE       0x8000

// ── API stubs (link-time bodies in send-message.cpp) ──────────
LPITEMIDLIST WINAPI SHBrowseForFolderW(LPBROWSEINFOW lpbi);
LPITEMIDLIST WINAPI SHBrowseForFolder (LPBROWSEINFO  lpbi);
BOOL         WINAPI SHGetPathFromIDListW(LPCITEMIDLIST pidl, LPWSTR pszPath);
BOOL         WINAPI SHGetPathFromIDList (LPCITEMIDLIST pidl, LPSTR  pszPath);
HRESULT      WINAPI SHGetSpecialFolderLocation(HWND hwnd, int csidl,
                                                 LPITEMIDLIST *ppidl);
HRESULT      WINAPI SHGetFolderPathW(HWND hwnd, int csidl, HANDLE hToken,
                                       DWORD dwFlags, LPWSTR pszPath);
HRESULT      WINAPI SHGetFolderPathA(HWND hwnd, int csidl, HANDLE hToken,
                                       DWORD dwFlags, LPSTR  pszPath);
// CoTaskMemFree alias used to free the LPITEMIDLIST result.
void         WINAPI CoTaskMemFree(void *pv);

// IMalloc — OLE allocator interface.  ml_playlists calls
// SHGetMalloc to obtain one for FreePIDL-style cleanup.  Stub
// with a vtable that routes Alloc through malloc, Free through
// free.
struct IMalloc {
    void *(*Alloc)        (struct IMalloc *, SIZE_T);
    void *(*Realloc)      (struct IMalloc *, void *, SIZE_T);
    void  (*Free)         (struct IMalloc *, void *);
    SIZE_T(*GetSize)      (struct IMalloc *, void *);
    int   (*DidAlloc)     (struct IMalloc *, void *);
    void  (*HeapMinimize) (struct IMalloc *);
};
HRESULT WINAPI SHGetMalloc(struct IMalloc **ppMalloc);

// SHFILEOPSTRUCTW + SHFileOperationW — shell file operations.
typedef struct _SHFILEOPSTRUCTW {
    HWND     hwnd;
    UINT     wFunc;
    LPCWSTR  pFrom;
    LPCWSTR  pTo;
    UINT     fFlags;
    BOOL     fAnyOperationsAborted;
    LPVOID   hNameMappings;
    LPCWSTR  lpszProgressTitle;
} SHFILEOPSTRUCTW, *LPSHFILEOPSTRUCTW;
typedef SHFILEOPSTRUCTW SHFILEOPSTRUCT;
typedef LPSHFILEOPSTRUCTW LPSHFILEOPSTRUCT;

int WINAPI SHFileOperationW(LPSHFILEOPSTRUCTW);
int WINAPI SHFileOperationA(void *);
#if defined(UNICODE) || defined(_UNICODE)
#  define SHFileOperation SHFileOperationW
#endif

// FO_* operation codes.
#define FO_MOVE                     0x0001
#define FO_COPY                     0x0002
#define FO_DELETE                   0x0003
#define FO_RENAME                   0x0004

// FOF_* flags.
#define FOF_MULTIDESTFILES          0x0001
#define FOF_CONFIRMMOUSE            0x0002
#define FOF_SILENT                  0x0004
#define FOF_RENAMEONCOLLISION       0x0008
#define FOF_NOCONFIRMATION          0x0010
#define FOF_WANTMAPPINGHANDLE       0x0020
#define FOF_ALLOWUNDO               0x0040
#define FOF_FILESONLY               0x0080
#define FOF_SIMPLEPROGRESS          0x0100
#define FOF_NOCONFIRMMKDIR          0x0200
#define FOF_NOERRORUI               0x0400
#define FOF_NOCOPYSECURITYATTRIBS   0x0800
#define FOF_NORECURSION             0x1000
#define FOF_NO_CONNECTED_ELEMENTS   0x2000
#define FOF_WANTNUKEWARNING         0x4000

#if defined(UNICODE) || defined(_UNICODE)
#  define SHGetFolderPath SHGetFolderPathW
#else
#  define SHGetFolderPath SHGetFolderPathA
#endif

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // _SHLOBJ_H_INCLUDED_
