// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#ifndef _ML_IPC_H_INCLUDED_
#define _ML_IPC_H_INCLUDED_
//
// ml_ipc.h — gen_ml's per-library private IPC surface.
//
// The IPC pattern: `MLNavCtrl_*` / `MLNavItem_*` macros expand into
// `SendMessageW(hwndLibrary, WM_ML_IPC, wparam=data,
// lparam=ML_IPC_*)`.  The receiving window (gen_ml's library
// HWND) dispatches the LPARAM-tagged sub-message.
//
// This header provides:
//   * The `NAVITEM` / `NAVINSERTSTRUCT` struct family.
//   * The `NIMF_*` / `NIS_*` / `NCS_*` flag constants.
//   * The `MLNavCtrl_*` macro family routing through SENDMLIPC.
//   * Enough `ML_IPC_*` enum values for ml_nowplaying's nav
//     registrations (insert, delete, find-by-id, get-image-list,
//     begin/end update).
//
// The receiver side lives in qtwasabi/src/ml/MlLibraryWindow.cpp
// — a WindowObject subclass that translates ML_IPC_NAVCTRL_*
// into operations on an embedded TreeListWidget.
//

#include "basetsd.h"
#include "windef.h"
#include "winuser.h"
#include "commctrl.h"   // HIMAGELIST, HTREEITEM

#ifdef __cplusplus
extern "C" {
#endif

// Custom Win32 message gen_ml reserved for ml_* IPC.
// The Winamp API defines this as `WM_USER + 0x1000`
// = 0x1400.  Plugin code uses that value when sending; our
// MlLibraryWindow handler must dispatch on the same number.
#ifndef WM_ML_IPC
#  define WM_ML_IPC      (WM_USER + 0x1000)
#endif

// Convenience macro mirror.
#define SENDMLIPC(__hwndML, __ipcMsgId, __param) \
    SendMessageW((__hwndML), WM_ML_IPC,            \
                  (WPARAM)(__param), (LPARAM)(__ipcMsgId))

// ── Navigation primitives ──────────────────────────────────────

typedef LPVOID HNAVITEM;

// Image list owned by the nav control.
typedef HIMAGELIST HMLIMGLST;

typedef struct _NAVITEM {
    INT       cbSize;
    HNAVITEM  hItem;
    UINT      mask;
    INT       id;
    LPWSTR    pszText;
    INT       cchTextMax;
    LPWSTR    pszInvariant;
    INT       cchInvariantMax;
    INT       iImage;
    INT       iSelectedImage;
    UINT      state;
    UINT      stateMask;
    UINT      style;
    UINT      styleMask;
    HFONT     hFont;
    LPARAM    lParam;
} NAVITEM;

typedef struct _NAVINSERTSTRUCT {
    HNAVITEM  hParent;
    HNAVITEM  hInsertAfter;
    NAVITEM   item;
} NAVINSERTSTRUCT;

// Insert-after sentinels (drop into NAVINSERTSTRUCT.hInsertAfter).
#define NCI_FIRST   ((HNAVITEM)0)
#define NCI_LAST    ((HNAVITEM)(uintptr_t)0xFFFFFFFFu)

// ── ML_IPC_* constants ─────────────────────────────────────────
// Numerical values match the Winamp API so a gen_ml-style payload
// that hardcoded `0x128C` is wire-compat.

#define ML_IPC_NAVIGATION_FIRST            0x1280L

#define ML_IPC_NAVCTRL_BEGINUPDATE         (ML_IPC_NAVIGATION_FIRST +  0)
#define ML_IPC_NAVCTRL_DELETEITEM          (ML_IPC_NAVIGATION_FIRST +  2)
#define ML_IPC_NAVCTRL_ENDUPDATE           (ML_IPC_NAVIGATION_FIRST +  3)
#define ML_IPC_NAVCTRL_ENUMITEMS           (ML_IPC_NAVIGATION_FIRST +  4)
#define ML_IPC_NAVCTRL_FINDITEMBYID        (ML_IPC_NAVIGATION_FIRST +  5)
#define ML_IPC_NAVCTRL_FINDITEMBYNAME      (ML_IPC_NAVIGATION_FIRST +  6)
#define ML_IPC_NAVCTRL_GETIMAGELIST        (ML_IPC_NAVIGATION_FIRST +  7)
#define ML_IPC_NAVCTRL_GETFIRST            (ML_IPC_NAVIGATION_FIRST +  8)
#define ML_IPC_NAVCTRL_GETINDENT           (ML_IPC_NAVIGATION_FIRST +  9)
#define ML_IPC_NAVCTRL_GETSELECTION        (ML_IPC_NAVIGATION_FIRST + 10)
#define ML_IPC_NAVCTRL_GETHWND             (ML_IPC_NAVIGATION_FIRST + 11)
#define ML_IPC_NAVCTRL_INSERTITEM          (ML_IPC_NAVIGATION_FIRST + 12)
#define ML_IPC_NAVCTRL_HITTEST             (ML_IPC_NAVIGATION_FIRST + 13)
#define ML_IPC_NAVCTRL_GETSTYLE            (ML_IPC_NAVIGATION_FIRST + 14)
#define ML_IPC_NAVCTRL_ENDEDITTITLE        (ML_IPC_NAVIGATION_FIRST + 15)

#define ML_IPC_NAVITEM_FIRST               0x12A0L
#define ML_IPC_NAVITEM_EDITTITLE           (ML_IPC_NAVITEM_FIRST +  0)
#define ML_IPC_NAVITEM_ENSUREVISIBLE       (ML_IPC_NAVITEM_FIRST +  1)
#define ML_IPC_NAVITEM_EXPAND              (ML_IPC_NAVITEM_FIRST +  2)
#define ML_IPC_NAVITEM_GETCHILD            (ML_IPC_NAVITEM_FIRST +  3)
#define ML_IPC_NAVITEM_GETINFO             (ML_IPC_NAVITEM_FIRST +  4)
#define ML_IPC_NAVITEM_SETINFO             (ML_IPC_NAVITEM_FIRST +  5)
#define ML_IPC_NAVITEM_GETPARENT           (ML_IPC_NAVITEM_FIRST +  6)
#define ML_IPC_NAVITEM_GETNEXT             (ML_IPC_NAVITEM_FIRST +  7)
#define ML_IPC_NAVITEM_GETPREV             (ML_IPC_NAVITEM_FIRST +  8)
#define ML_IPC_NAVITEM_GETRECT             (ML_IPC_NAVITEM_FIRST +  9)
#define ML_IPC_NAVITEM_INVALIDATE          (ML_IPC_NAVITEM_FIRST + 10)
#define ML_IPC_NAVITEM_MOVE                (ML_IPC_NAVITEM_FIRST + 11)
#define ML_IPC_NAVITEM_REMOVECHILDREN      (ML_IPC_NAVITEM_FIRST + 12)
#define ML_IPC_NAVITEM_SELECT              (ML_IPC_NAVITEM_FIRST + 13)
#define ML_IPC_NAVITEM_GETFULLNAME         (ML_IPC_NAVITEM_FIRST + 14)
#define ML_IPC_NAVITEM_SETORDER            (ML_IPC_NAVITEM_FIRST + 15)

// ImageList family routed via the library window (gen_ml owns
// the navigation image strip globally).
#define ML_IPC_IMAGELIST_FIRST             0x12C0L
#define ML_IPC_IMAGELIST_ADD               (ML_IPC_IMAGELIST_FIRST + 0)

// Plugin services + version.
#define ML_IPC_GETVERSION                  0x1100L
#define ML_IPC_GETLIBHANDLE                0x1101L

// ── Flag constants ─────────────────────────────────────────────

// NAVITEM mask flags.
#define NIMF_ITEMID            0x0001
#define NIMF_TEXT              0x0002
#define NIMF_TEXTINVARIANT     0x0004
#define NIMF_IMAGE             0x0008
#define NIMF_IMAGESEL          0x0010
#define NIMF_STATE             0x0020
#define NIMF_STYLE             0x0040
#define NIMF_FONT              0x0080
#define NIMF_PARAM             0x0100

// NAVITEM state.
#define NIS_NORMAL             0x0000
#define NIS_SELECTED           0x0001
#define NIS_EXPANDED           0x0002

// NAVITEM style.
#define NIS_NOCHILDREN         0x0001  // never has children
#define NIS_ALLOWCHILDMOVE     0x0002  // children can be dragged out
#define NIS_BOLD               0x0004
#define NIS_USEDEFAULTIMAGE    0x0008

// NAVCTRL styles.
#define NCS_NORMAL             0x0000
#define NCS_FULLROWSELECT      0x0001
#define NCS_SHOWICONS          0x0002

// Sort-order encoder: packs an order value into an HNAVITEM so it
// can be passed through NAVINSERTSTRUCT.hInsertAfter alongside the
// NCI_* sentinels.
#define MAKE_NAVITEMSORTORDER(o) ((HNAVITEM)((ULONG_PTR)((WORD)(o))))
#define IS_NAVITEMSORTORDER(_i)  ((((ULONG_PTR)(_i)) >> 16) == 0)

// ── Macro family ───────────────────────────────────────────────

#define MLNavCtrl_BeginUpdate(hwndML, fLockFlags) \
    ((INT)SENDMLIPC((hwndML), ML_IPC_NAVCTRL_BEGINUPDATE, (WPARAM)(fLockFlags)))

#define MLNavCtrl_EndUpdate(hwndML) \
    ((INT)SENDMLIPC((hwndML), ML_IPC_NAVCTRL_ENDUPDATE, (WPARAM)0))

#define MLNavCtrl_InsertItem(hwndML, pnavInsert) \
    ((HNAVITEM)SENDMLIPC((hwndML), ML_IPC_NAVCTRL_INSERTITEM, \
                          (WPARAM)(pnavInsert)))

#define MLNavCtrl_DeleteItem(hwndML, hItem) \
    ((INT)SENDMLIPC((hwndML), ML_IPC_NAVCTRL_DELETEITEM, (WPARAM)(hItem)))

#define MLNavCtrl_FindItemById(hwndML, itemId) \
    ((HNAVITEM)SENDMLIPC((hwndML), ML_IPC_NAVCTRL_FINDITEMBYID, \
                          (WPARAM)(itemId)))

#define MLNavCtrl_GetImageList(hwndML) \
    ((HMLIMGLST)SENDMLIPC((hwndML), ML_IPC_NAVCTRL_GETIMAGELIST, (WPARAM)0))

#define MLNavCtrl_GetSelection(hwndML) \
    ((HNAVITEM)SENDMLIPC((hwndML), ML_IPC_NAVCTRL_GETSELECTION, (WPARAM)0))

#define MLNavCtrl_GetFirst(hwndML) \
    ((HNAVITEM)SENDMLIPC((hwndML), ML_IPC_NAVCTRL_GETFIRST, (WPARAM)0))

#define MLNavItem_Select(hwndML, hItem) \
    ((BOOL)SENDMLIPC((hwndML), ML_IPC_NAVITEM_SELECT, (WPARAM)(hItem)))

#define MLNavItem_EnsureVisible(hwndML, hItem) \
    ((BOOL)SENDMLIPC((hwndML), ML_IPC_NAVITEM_ENSUREVISIBLE, (WPARAM)(hItem)))

#define GetMLVersion(hwndML) \
    ((DWORD)SENDMLIPC((hwndML), ML_IPC_GETVERSION, (WPARAM)0))

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // _ML_IPC_H_INCLUDED_
