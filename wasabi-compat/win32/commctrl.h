// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#ifndef _COMMCTRL_H
#define _COMMCTRL_H
//
// commctrl.h — Win32 Common Controls API surface used by gen_ml and
// its ml_* sub-plugins.  Provides the message constants (TVM_*,
// LVM_*, HDM_*), notification codes (TVN_*, LVN_*, HDN_*) and the
// struct layouts (TVITEM, LVITEM, HDITEM, NMTREEVIEW, NMLISTVIEW,
// IMAGEINFO).
//
// These are DECLARATIONS only.  They carry no rendering or
// state-mutation behaviour; that lives in qtWasabi's
// TreeListWidget / MultiColumnListWidget primitives and the
// TVM_* / LVM_* dispatch that drives them.
//
// The constant values match the Win32 SDK so binary-format
// payloads in gen_ml's code (insert structs, sort lparams)
// stay wire-compatible.
//

#include "basetsd.h"
#include "windef.h"
#include "winuser.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── ImageList ──────────────────────────────────────────────────
//
// HIMAGELIST is the universal "icon strip" Win32 controls use to
// render per-row glyphs.  ml_* plugins build one image list per
// plugin (ml_local's category icons, ml_devices' connector icons,
// …) and pass it into the TreeView via TVM_SETIMAGELIST.

#define ILC_MASK              0x00000001
#define ILC_COLOR             0x00000000
#define ILC_COLOR4            0x00000004
#define ILC_COLOR8            0x00000008
#define ILC_COLOR16           0x00000010
#define ILC_COLOR24           0x00000018
#define ILC_COLOR32           0x00000020
#define ILC_PALETTE           0x00000800

typedef struct _IMAGEINFO {
    HBITMAP hbmImage;
    HBITMAP hbmMask;
    int     Unused1;
    int     Unused2;
    RECT    rcImage;
} IMAGEINFO, *LPIMAGEINFO;

// Function-style entry points (defined in win32/commctrl-stubs.cpp).
HIMAGELIST WINAPI ImageList_Create  (int cx, int cy, UINT flags, int initial, int grow);
BOOL       WINAPI ImageList_Destroy (HIMAGELIST himl);
int        WINAPI ImageList_Add     (HIMAGELIST himl, HBITMAP hbmImage, HBITMAP hbmMask);
int        WINAPI ImageList_AddIcon (HIMAGELIST himl, HICON hicon);
int        WINAPI ImageList_GetImageCount(HIMAGELIST himl);
BOOL       WINAPI ImageList_GetImageInfo (HIMAGELIST himl, int i, IMAGEINFO *pii);
BOOL       WINAPI ImageList_Remove  (HIMAGELIST himl, int i);

// ── TreeView ───────────────────────────────────────────────────
//
// TreeView_* messages drive gen_ml's navigation control.  Each
// node is identified by HTREEITEM, an opaque handle.  The
// constants and struct layouts here give gen_ml a wire-compatible
// API to compile and dispatch against.

DECLARE_HANDLE(HTREEITEM);

#define TVS_HASBUTTONS         0x0001
#define TVS_HASLINES           0x0002
#define TVS_LINESATROOT        0x0004
#define TVS_EDITLABELS         0x0008
#define TVS_DISABLEDRAGDROP    0x0010
#define TVS_SHOWSELALWAYS      0x0020
#define TVS_CHECKBOXES         0x0100
#define TVS_TRACKSELECT        0x0200
#define TVS_FULLROWSELECT      0x1000

#define TVIF_TEXT              0x0001
#define TVIF_IMAGE             0x0002
#define TVIF_PARAM             0x0004
#define TVIF_STATE             0x0008
#define TVIF_HANDLE            0x0010
#define TVIF_SELECTEDIMAGE     0x0020
#define TVIF_CHILDREN          0x0040
#define TVIF_INTEGRAL          0x0080

#define TVIS_SELECTED          0x0002
#define TVIS_CUT               0x0004
#define TVIS_DROPHILITED       0x0008
#define TVIS_BOLD              0x0010
#define TVIS_EXPANDED          0x0020
#define TVIS_EXPANDEDONCE      0x0040
#define TVIS_EXPANDPARTIAL     0x0080

#define TVI_ROOT               ((HTREEITEM)(ULONG_PTR)-0x10000)
#define TVI_FIRST              ((HTREEITEM)(ULONG_PTR)-0x0FFFF)
#define TVI_LAST               ((HTREEITEM)(ULONG_PTR)-0x0FFFE)
#define TVI_SORT               ((HTREEITEM)(ULONG_PTR)-0x0FFFD)

typedef struct tagTVITEMW {
    UINT       mask;
    HTREEITEM  hItem;
    UINT       state;
    UINT       stateMask;
    LPWSTR     pszText;
    int        cchTextMax;
    int        iImage;
    int        iSelectedImage;
    int        cChildren;
    LPARAM     lParam;
} TVITEMW, *LPTVITEMW;

typedef TVITEMW    TV_ITEM;
typedef LPTVITEMW  LPTV_ITEM;
typedef TVITEMW    TVITEM;
typedef LPTVITEMW  LPTVITEM;

typedef struct tagTVINSERTSTRUCTW {
    HTREEITEM hParent;
    HTREEITEM hInsertAfter;
    union {
        TVITEMW  itemex;
        TVITEMW  item;
    };
} TVINSERTSTRUCTW, *LPTVINSERTSTRUCTW;

typedef TVINSERTSTRUCTW    TV_INSERTSTRUCT;
typedef LPTVINSERTSTRUCTW  LPTV_INSERTSTRUCT;

// TreeView messages (WM_USER + offset).
#define TV_FIRST                   0x1100
#define TVM_INSERTITEMW            (TV_FIRST + 50)
#define TVM_INSERTITEMA            (TV_FIRST + 0)
#define TVM_DELETEITEM             (TV_FIRST + 1)
#define TVM_EXPAND                 (TV_FIRST + 2)
#define TVM_GETITEMRECT            (TV_FIRST + 4)
#define TVM_GETCOUNT               (TV_FIRST + 5)
#define TVM_GETINDENT              (TV_FIRST + 6)
#define TVM_SETINDENT              (TV_FIRST + 7)
#define TVM_GETIMAGELIST           (TV_FIRST + 8)
#define TVM_SETIMAGELIST           (TV_FIRST + 9)
#define TVM_GETNEXTITEM            (TV_FIRST + 10)
#define TVM_SELECTITEM             (TV_FIRST + 11)
#define TVM_GETITEMW               (TV_FIRST + 62)
#define TVM_GETITEMA               (TV_FIRST + 12)
#define TVM_SETITEMW               (TV_FIRST + 63)
#define TVM_SETITEMA               (TV_FIRST + 13)
#define TVM_HITTEST                (TV_FIRST + 17)
#define TVM_ENSUREVISIBLE          (TV_FIRST + 20)
#define TVM_SORTCHILDRENCB         (TV_FIRST + 21)
#define TVM_GETSELECTEDCOUNT       (TV_FIRST + 70)

// Convenience macros wrapping SendMessage.
#define TreeView_InsertItem(hwnd, lpis)         \
    ((HTREEITEM)SendMessageW((hwnd), TVM_INSERTITEMW, 0, (LPARAM)(LPTV_INSERTSTRUCT)(lpis)))
#define TreeView_DeleteItem(hwnd, hitem)        \
    ((BOOL)SendMessageW((hwnd), TVM_DELETEITEM, 0, (LPARAM)(HTREEITEM)(hitem)))
#define TreeView_DeleteAllItems(hwnd)           \
    ((BOOL)SendMessageW((hwnd), TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT))
#define TreeView_SelectItem(hwnd, hitem)        \
    ((BOOL)SendMessageW((hwnd), TVM_SELECTITEM, TVGN_CARET, (LPARAM)(HTREEITEM)(hitem)))
#define TreeView_GetSelection(hwnd)             \
    ((HTREEITEM)SendMessageW((hwnd), TVM_GETNEXTITEM, TVGN_CARET, 0))
#define TreeView_GetItem(hwnd, pitem)           \
    ((BOOL)SendMessageW((hwnd), TVM_GETITEMW, 0, (LPARAM)(LPTVITEM)(pitem)))
#define TreeView_SetItem(hwnd, pitem)           \
    ((BOOL)SendMessageW((hwnd), TVM_SETITEMW, 0, (LPARAM)(LPTVITEM)(pitem)))
#define TreeView_Expand(hwnd, hitem, code)      \
    ((BOOL)SendMessageW((hwnd), TVM_EXPAND, (WPARAM)(code), (LPARAM)(HTREEITEM)(hitem)))

// TVGN_* — TVM_GETNEXTITEM relationship codes.
#define TVGN_ROOT              0x0000
#define TVGN_NEXT              0x0001
#define TVGN_PREVIOUS          0x0002
#define TVGN_PARENT            0x0003
#define TVGN_CHILD             0x0004
#define TVGN_FIRSTVISIBLE      0x0005
#define TVGN_NEXTVISIBLE       0x0006
#define TVGN_PREVIOUSVISIBLE   0x0007
#define TVGN_DROPHILITE        0x0008
#define TVGN_CARET             0x0009

// TVE_* — TVM_EXPAND action codes.
#define TVE_COLLAPSE           0x0001
#define TVE_EXPAND             0x0002
#define TVE_TOGGLE             0x0003
#define TVE_COLLAPSERESET      0x8000

// Notification codes (sent via WM_NOTIFY).
#define TVN_FIRST              ((UINT)(0u-400u))
#define TVN_LAST               ((UINT)(0u-499u))
#define TVN_SELCHANGINGW       (TVN_FIRST-50)
#define TVN_SELCHANGEDW        (TVN_FIRST-51)
#define TVN_GETDISPINFOW       (TVN_FIRST-52)
#define TVN_SETDISPINFOW       (TVN_FIRST-53)
#define TVN_ITEMEXPANDINGW     (TVN_FIRST-54)
#define TVN_ITEMEXPANDEDW      (TVN_FIRST-55)
#define TVN_BEGINDRAGW         (TVN_FIRST-56)
#define TVN_BEGINRDRAGW        (TVN_FIRST-57)
#define TVN_DELETEITEMW        (TVN_FIRST-58)
#define TVN_BEGINLABELEDITW    (TVN_FIRST-59)
#define TVN_ENDLABELEDITW      (TVN_FIRST-60)
#define TVN_KEYDOWN            (TVN_FIRST-12)

typedef struct tagNMTREEVIEWW {
    NMHDR     hdr;
    UINT      action;
    TVITEMW   itemOld;
    TVITEMW   itemNew;
    POINT     ptDrag;
} NMTREEVIEWW, *LPNMTREEVIEWW;

typedef NMTREEVIEWW    NM_TREEVIEW;
typedef LPNMTREEVIEWW  LPNM_TREEVIEW;
typedef NMTREEVIEWW    NMTREEVIEW;

// NMTVKEYDOWN — TreeView WM_NOTIFY keydown payload.
typedef struct tagTVKEYDOWN {
    NMHDR hdr;
    WORD  wVKey;
    UINT  flags;
} NMTVKEYDOWN, *LPNMTVKEYDOWN;

typedef struct tagTVDISPINFOW {
    NMHDR    hdr;
    TVITEMW  item;
} NMTVDISPINFOW, *LPNMTVDISPINFOW;

typedef NMTVDISPINFOW    TV_DISPINFO;

// ── ListView ───────────────────────────────────────────────────

#define LVS_ICON              0x0000
#define LVS_REPORT            0x0001
#define LVS_SMALLICON         0x0002
#define LVS_LIST              0x0003
#define LVS_TYPEMASK          0x0003
#define LVS_SINGLESEL         0x0004
#define LVS_SHOWSELALWAYS     0x0008
#define LVS_SORTASCENDING     0x0010
#define LVS_SORTDESCENDING    0x0020
#define LVS_SHAREIMAGELISTS   0x0040
#define LVS_NOLABELWRAP       0x0080
#define LVS_AUTOARRANGE       0x0100
#define LVS_EDITLABELS        0x0200
#define LVS_OWNERDATA         0x1000
#define LVS_NOSCROLL          0x2000
#define LVS_OWNERDRAWFIXED    0x0400
#define LVS_NOCOLUMNHEADER    0x4000
#define LVS_NOSORTHEADER      0x8000

#define LVIF_TEXT              0x0001
#define LVIF_IMAGE             0x0002
#define LVIF_PARAM             0x0004
#define LVIF_STATE             0x0008
#define LVIF_INDENT            0x0010
#define LVIF_GROUPID           0x0100
#define LVIF_COLUMNS           0x0200

#define LVIS_FOCUSED           0x0001
#define LVIS_SELECTED          0x0002
#define LVIS_CUT               0x0004
#define LVIS_DROPHILITED       0x0008

#define LVCF_FMT               0x0001
#define LVCF_WIDTH             0x0002
#define LVCF_TEXT              0x0004
#define LVCF_SUBITEM           0x0008
#define LVCF_IMAGE             0x0010
#define LVCF_ORDER             0x0020

#define LVCFMT_LEFT            0x0000
#define LVCFMT_RIGHT           0x0001
#define LVCFMT_CENTER          0x0002
#define LVCFMT_JUSTIFYMASK     0x0003
#define LVCFMT_IMAGE           0x0800
#define LVCFMT_BITMAP_ON_RIGHT 0x1000
#define LVCFMT_COL_HAS_IMAGES  0x8000

typedef struct tagLVITEMW {
    UINT     mask;
    int      iItem;
    int      iSubItem;
    UINT     state;
    UINT     stateMask;
    LPWSTR   pszText;
    int      cchTextMax;
    int      iImage;
    LPARAM   lParam;
    int      iIndent;
    int      iGroupId;
    UINT     cColumns;
    PUINT    puColumns;
    int     *piColFmt;
    int      iGroup;
} LVITEMW, *LPLVITEMW;

typedef LVITEMW    LVITEM;
typedef LPLVITEMW  LPLVITEM;
typedef LVITEMW    LV_ITEM;

typedef struct tagLVCOLUMNW {
    UINT     mask;
    int      fmt;
    int      cx;
    LPWSTR   pszText;
    int      cchTextMax;
    int      iSubItem;
    int      iImage;
    int      iOrder;
    int      cxMin;
    int      cxDefault;
    int      cxIdeal;
} LVCOLUMNW, *LPLVCOLUMNW;

typedef LVCOLUMNW    LVCOLUMN;
typedef LPLVCOLUMNW  LPLVCOLUMN;
typedef LVCOLUMNW    LV_COLUMN;

// ListView messages.
#define LVM_FIRST                  0x1000
#define LVM_GETBKCOLOR             (LVM_FIRST + 0)
#define LVM_SETBKCOLOR             (LVM_FIRST + 1)
#define LVM_GETIMAGELIST           (LVM_FIRST + 2)
#define LVM_SETIMAGELIST           (LVM_FIRST + 3)
#define LVM_GETITEMCOUNT           (LVM_FIRST + 4)
#define LVM_GETITEMW               (LVM_FIRST + 75)
#define LVM_SETITEMW               (LVM_FIRST + 76)
#define LVM_INSERTITEMW            (LVM_FIRST + 77)
#define LVM_DELETEITEM             (LVM_FIRST + 8)
#define LVM_DELETEALLITEMS         (LVM_FIRST + 9)
#define LVM_GETCOLUMNW             (LVM_FIRST + 95)
#define LVM_SETCOLUMNW             (LVM_FIRST + 96)
#define LVM_INSERTCOLUMNW          (LVM_FIRST + 97)
#define LVM_DELETECOLUMN           (LVM_FIRST + 28)
#define LVM_GETCOLUMNWIDTH         (LVM_FIRST + 29)
#define LVM_SETCOLUMNWIDTH         (LVM_FIRST + 30)
#define LVM_GETITEMSTATE           (LVM_FIRST + 44)
#define LVM_SETITEMSTATE           (LVM_FIRST + 43)
#define LVM_GETSELECTEDCOUNT       (LVM_FIRST + 50)
#define LVM_GETITEMTEXTW           (LVM_FIRST + 115)
#define LVM_SETITEMTEXTW           (LVM_FIRST + 116)
#define LVM_SORTITEMS              (LVM_FIRST + 48)
#define LVM_GETNEXTITEM            (LVM_FIRST + 12)
#define LVM_ENSUREVISIBLE          (LVM_FIRST + 19)
#define LVM_SETEXTENDEDLISTVIEWSTYLE (LVM_FIRST + 54)
#define LVM_GETEXTENDEDLISTVIEWSTYLE (LVM_FIRST + 55)
#define LVM_GETITEMRECT            (LVM_FIRST + 14)
#define LVM_GETSUBITEMRECT         (LVM_FIRST + 56)
#define LVM_SETITEMCOUNT           (LVM_FIRST + 47)
#define LVM_REDRAWITEMS            (LVM_FIRST + 21)
#define LVM_GETSELECTIONMARK       (LVM_FIRST + 66)
#define LVM_SETSELECTIONMARK       (LVM_FIRST + 67)

// Non-W aliases for code that didn't UNICODE-ify on Win32.
#define LVM_GETITEMTEXT            LVM_GETITEMTEXTW
#define LVM_SETITEMTEXT            LVM_SETITEMTEXTW
#define LVM_INSERTCOLUMN           LVM_INSERTCOLUMNW
#define LVM_GETCOLUMN              LVM_GETCOLUMNW
#define LVM_SETCOLUMN              LVM_SETCOLUMNW
#define LVM_GETITEM                LVM_GETITEMW
#define LVM_SETITEM                LVM_SETITEMW
#define LVM_INSERTITEM             LVM_INSERTITEMW

// LVNI_* — next-item search flags.
#define LVNI_ALL                   0x0000
#define LVNI_FOCUSED               0x0001
#define LVNI_SELECTED              0x0002
#define LVNI_CUT                   0x0004
#define LVNI_DROPHILITED           0x0008
#define LVNI_ABOVE                 0x0100
#define LVNI_BELOW                 0x0200
#define LVNI_TOLEFT                0x0400
#define LVNI_TORIGHT               0x0800

// Extended ListView styles (LVM_SETEXTENDEDLISTVIEWSTYLE).
#define LVS_EX_GRIDLINES        0x00000001
#define LVS_EX_HEADERDRAGDROP   0x00000010
#define LVS_EX_FULLROWSELECT    0x00000020
#define LVS_EX_CHECKBOXES       0x00000004
#define LVS_EX_TRACKSELECT      0x00000008
#define LVS_EX_DOUBLEBUFFER     0x00010000

// Convenience macros.
#define ListView_InsertItem(hwnd, pitem)        \
    ((int)SendMessageW((hwnd), LVM_INSERTITEMW, 0, (LPARAM)(LPLVITEM)(pitem)))
#define ListView_DeleteItem(hwnd, i)            \
    ((BOOL)SendMessageW((hwnd), LVM_DELETEITEM, (WPARAM)(i), 0))
#define ListView_DeleteAllItems(hwnd)           \
    ((BOOL)SendMessageW((hwnd), LVM_DELETEALLITEMS, 0, 0))
#define ListView_GetItemCount(hwnd)             \
    ((int)SendMessageW((hwnd), LVM_GETITEMCOUNT, 0, 0))
#define ListView_InsertColumn(hwnd, i, pcol)    \
    ((int)SendMessageW((hwnd), LVM_INSERTCOLUMNW, (WPARAM)(i), (LPARAM)(LPLVCOLUMN)(pcol)))
#define ListView_SetColumnWidth(hwnd, i, cx)    \
    ((BOOL)SendMessageW((hwnd), LVM_SETCOLUMNWIDTH, (WPARAM)(i), (LPARAM)(cx)))
#define ListView_GetColumnWidth(hwnd, i)        \
    ((int)SendMessageW((hwnd), LVM_GETCOLUMNWIDTH, (WPARAM)(i), 0))
#define ListView_SetExtendedListViewStyle(hwnd, ex) \
    ((DWORD)SendMessageW((hwnd), LVM_SETEXTENDEDLISTVIEWSTYLE, 0, (LPARAM)(ex)))
#define ListView_SortItems(hwnd, cb, lp)        \
    ((BOOL)SendMessageW((hwnd), LVM_SORTITEMS, (WPARAM)(lp), (LPARAM)(PFNLVCOMPARE)(cb)))

// Notifications.
#define LVN_FIRST              ((UINT)(0u-100u))
#define LVN_ITEMCHANGING       (LVN_FIRST-0)
#define LVN_ITEMCHANGED        (LVN_FIRST-1)
#define LVN_INSERTITEM         (LVN_FIRST-2)
#define LVN_DELETEITEM         (LVN_FIRST-3)
#define LVN_DELETEALLITEMS     (LVN_FIRST-4)
#define LVN_COLUMNCLICK        (LVN_FIRST-8)
#define LVN_BEGINDRAG          (LVN_FIRST-9)
#define LVN_BEGINRDRAG         (LVN_FIRST-11)
#define LVN_GETDISPINFOW       (LVN_FIRST-77)
#define LVN_SETDISPINFOW       (LVN_FIRST-78)
#define LVN_KEYDOWN            (LVN_FIRST-55)
#define LVN_ODSTATECHANGED     (LVN_FIRST-15)
#define LVN_ODFINDITEMW        (LVN_FIRST-79)
#define LVN_ITEMACTIVATE       (LVN_FIRST-14)

typedef struct tagNMLISTVIEW {
    NMHDR     hdr;
    int       iItem;
    int       iSubItem;
    UINT      uNewState;
    UINT      uOldState;
    UINT      uChanged;
    POINT     ptAction;
    LPARAM    lParam;
} NMLISTVIEW, *LPNMLISTVIEW;

typedef NMLISTVIEW NM_LISTVIEW;

typedef struct tagNMLVDISPINFOW {
    NMHDR    hdr;
    LVITEMW  item;
} NMLVDISPINFOW, *LPNMLVDISPINFOW;

typedef NMLVDISPINFOW LV_DISPINFO;

typedef int (CALLBACK *PFNLVCOMPARE)(LPARAM, LPARAM, LPARAM);

// ── Header control ─────────────────────────────────────────────

#define HDM_FIRST              0x1200
#define HDM_GETITEMCOUNT       (HDM_FIRST + 0)
#define HDM_INSERTITEMW        (HDM_FIRST + 10)
#define HDM_DELETEITEM         (HDM_FIRST + 2)
#define HDM_GETITEMW           (HDM_FIRST + 11)
#define HDM_SETITEMW           (HDM_FIRST + 12)
#define HDM_LAYOUT             (HDM_FIRST + 5)
#define HDM_HITTEST            (HDM_FIRST + 6)
#define HDM_GETITEMRECT        (HDM_FIRST + 7)

#define HDI_WIDTH              0x0001
#define HDI_HEIGHT             HDI_WIDTH
#define HDI_TEXT               0x0002
#define HDI_FORMAT             0x0004
#define HDI_LPARAM             0x0008
#define HDI_BITMAP             0x0010
#define HDI_IMAGE              0x0020
#define HDI_DI_SETITEM         0x0040
#define HDI_ORDER              0x0080
#define HDI_FILTER             0x0100

#define HDF_LEFT               0x0000
#define HDF_RIGHT              0x0001
#define HDF_CENTER             0x0002
#define HDF_JUSTIFYMASK        0x0003
#define HDF_RTLREADING         0x0004
#define HDF_STRING             0x4000
#define HDF_BITMAP             0x2000
#define HDF_IMAGE              0x0800
#define HDF_SORTUP             0x0400
#define HDF_SORTDOWN           0x0200

typedef struct tagHDITEMW {
    UINT     mask;
    int      cxy;
    LPWSTR   pszText;
    HBITMAP  hbm;
    int      cchTextMax;
    int      fmt;
    LPARAM   lParam;
    int      iImage;
    int      iOrder;
    UINT     type;
    void    *pvFilter;
} HDITEMW, *LPHDITEMW;

typedef HDITEMW HDITEM;

#define HDN_FIRST              ((UINT)(0u-300u))
#define HDN_BEGINDRAG          (HDN_FIRST-10)
#define HDN_ENDDRAG            (HDN_FIRST-11)
#define HDN_ITEMCHANGINGW      (HDN_FIRST-20)
#define HDN_ITEMCHANGEDW       (HDN_FIRST-21)
#define HDN_ITEMCLICKW         (HDN_FIRST-22)
#define HDN_DIVIDERDBLCLICKW   (HDN_FIRST-25)
#define HDN_BEGINTRACKW        (HDN_FIRST-26)
#define HDN_ENDTRACKW          (HDN_FIRST-27)
#define HDN_TRACKW             (HDN_FIRST-28)

// ── Custom-draw notification ──────────────────────────────────
//
// gen_ml uses NM_CUSTOMDRAW to paint owner-draw rows in its
// SkinnedListView.  This is wired through to QPainter via
// the WindowObject's paint() path.

#define NM_FIRST               ((UINT)(0u-0u))
#define NM_CUSTOMDRAW          (NM_FIRST-12)

typedef struct tagNMCUSTOMDRAWINFO {
    NMHDR     hdr;
    DWORD     dwDrawStage;
    HDC       hdc;
    RECT      rc;
    DWORD_PTR dwItemSpec;
    UINT      uItemState;
    LPARAM    lItemlParam;
} NMCUSTOMDRAW, *LPNMCUSTOMDRAW;

typedef struct tagNMLVCUSTOMDRAW {
    NMCUSTOMDRAW nmcd;
    COLORREF     clrText;
    COLORREF     clrTextBk;
    int          iSubItem;
    DWORD        dwItemType;
    COLORREF     clrFace;
    int          iIconEffect;
    int          iIconPhase;
    int          iPartId;
    int          iStateId;
    RECT         rcText;
    UINT         uAlign;
} NMLVCUSTOMDRAW, *LPNMLVCUSTOMDRAW;

// CDRF_* — custom-draw return codes.
#define CDRF_DODEFAULT             0x00000000
#define CDRF_NEWFONT               0x00000002
#define CDRF_SKIPDEFAULT           0x00000004
#define CDRF_NOTIFYPOSTPAINT       0x00000010
#define CDRF_NOTIFYITEMDRAW        0x00000020
#define CDRF_NOTIFYSUBITEMDRAW     0x00000020
#define CDRF_NOTIFYPOSTERASE       0x00000040

// CDDS_* — custom-draw stage codes (sent in NMCUSTOMDRAW::dwDrawStage).
#define CDDS_PREPAINT              0x00000001
#define CDDS_POSTPAINT             0x00000002
#define CDDS_PREERASE              0x00000003
#define CDDS_POSTERASE             0x00000004
#define CDDS_ITEM                  0x00010000
#define CDDS_ITEMPREPAINT          (CDDS_ITEM | CDDS_PREPAINT)
#define CDDS_ITEMPOSTPAINT         (CDDS_ITEM | CDDS_POSTPAINT)
#define CDDS_SUBITEM               0x00020000

// CDIS_* — custom-draw item-state codes.
#define CDIS_SELECTED              0x0001
#define CDIS_GRAYED                0x0002
#define CDIS_DISABLED              0x0004
#define CDIS_CHECKED               0x0008
#define CDIS_FOCUS                 0x0010
#define CDIS_DEFAULT               0x0020
#define CDIS_HOT                   0x0040
#define CDIS_MARKED                0x0080
#define CDIS_INDETERMINATE         0x0100

// ── InitCommonControls ─────────────────────────────────────────
//
// Win32 boot sequence calls this once to register the common-
// control window classes.  No-op for us; the WindowObject family
// is always available via the registry.

typedef struct tagINITCOMMONCONTROLSEX {
    DWORD dwSize;
    DWORD dwICC;
} INITCOMMONCONTROLSEX, *LPINITCOMMONCONTROLSEX;

#define ICC_LISTVIEW_CLASSES   0x00000001
#define ICC_TREEVIEW_CLASSES   0x00000002
#define ICC_BAR_CLASSES        0x00000004
#define ICC_TAB_CLASSES        0x00000008
#define ICC_UPDOWN_CLASS       0x00000010
#define ICC_PROGRESS_CLASS     0x00000020
#define ICC_HOTKEY_CLASS       0x00000040
#define ICC_ANIMATE_CLASS      0x00000080

BOOL WINAPI InitCommonControls(void);
BOOL WINAPI InitCommonControlsEx(const INITCOMMONCONTROLSEX *picce);

// ── NMHDR pointer alias ──────────────────────────────────────────
typedef NMHDR             *LPNMHDR;

// ── NMLVDISPINFO non-W alias ────────────────────────────────────
typedef NMLVDISPINFOW    NMLVDISPINFO;
typedef LPNMLVDISPINFOW  LPNMLVDISPINFO;

// ── ListView rect codes (subitem) ──────────────────────────────
#define LVIR_BOUNDS            0
#define LVIR_ICON              1
#define LVIR_LABEL             2
#define LVIR_SELECTBOUNDS      3

// ── ListView item state flags ──────────────────────────────────
#define LVIS_FOCUSED           0x0001
#define LVIS_SELECTED          0x0002
#define LVIS_CUT               0x0004
#define LVIS_DROPHILITED       0x0008
#define LVIS_OVERLAYMASK       0x0F00
#define LVIS_STATEIMAGEMASK    0xF000

// ── ListView column-width auto-size sentinels ──────────────────
#define LVSCW_AUTOSIZE              (-1)
#define LVSCW_AUTOSIZE_USEHEADER    (-2)

// ── ListView LVFINDINFO struct ─────────────────────────────────
typedef struct tagLVFINDINFOW {
    UINT     flags;
    LPCWSTR  psz;
    LPARAM   lParam;
    POINT    pt;
    UINT     vkDirection;
} LVFINDINFOW, *LPLVFINDINFOW;
typedef LVFINDINFOW    LVFINDINFO;
typedef LPLVFINDINFOW  LPLVFINDINFO;
typedef LVFINDINFOW    LV_FINDINFO;
#define LVFI_PARAM           0x0001
#define LVFI_STRING          0x0002
#define LVFI_SUBSTRING       0x0004
#define LVFI_PARTIAL         0x0008

// ── NMITEMACTIVATE (WM_NOTIFY LV item activate / click) ────────
typedef struct tagNMITEMACTIVATE {
    NMHDR  hdr;
    int    iItem;
    int    iSubItem;
    UINT   uNewState;
    UINT   uOldState;
    UINT   uChanged;
    POINT  ptAction;
    LPARAM lParam;
    UINT   uKeyFlags;
} NMITEMACTIVATE, *LPNMITEMACTIVATE;

// ── NM_ and LVN_ notification codes ────────────────────────────
#define NM_FIRST               (0U-0U)
#define NM_OUTOFMEMORY         (NM_FIRST-1)
#define NM_CLICK               (NM_FIRST-2)
#define NM_DBLCLK              (NM_FIRST-3)
#define NM_RETURN              (NM_FIRST-4)
#define NM_RCLICK              (NM_FIRST-5)
#define NM_RDBLCLK             (NM_FIRST-6)
#define NM_SETFOCUS            (NM_FIRST-7)
#define NM_KILLFOCUS           (NM_FIRST-8)
#define NM_CUSTOMDRAW          (NM_FIRST-12)
#define NM_HOVER               (NM_FIRST-13)
#define NM_NCHITTEST           (NM_FIRST-14)
#define NM_KEYDOWN             (NM_FIRST-15)
#define NM_RELEASEDCAPTURE     (NM_FIRST-16)

#define LVN_FIRST              (0U-100U)
#define LVN_ITEMCHANGING       (LVN_FIRST-0)
#define LVN_ITEMCHANGED        (LVN_FIRST-1)
#define LVN_INSERTITEM         (LVN_FIRST-2)
#define LVN_DELETEITEM         (LVN_FIRST-3)
#define LVN_DELETEALLITEMS     (LVN_FIRST-4)
#define LVN_BEGINLABELEDITW    (LVN_FIRST-75)
#define LVN_ENDLABELEDITW      (LVN_FIRST-76)
#define LVN_COLUMNCLICK        (LVN_FIRST-8)
#define LVN_BEGINDRAG          (LVN_FIRST-9)
#define LVN_BEGINRDRAG         (LVN_FIRST-11)
#define LVN_GETDISPINFOW       (LVN_FIRST-77)
#define LVN_SETDISPINFOW       (LVN_FIRST-78)
#define LVN_KEYDOWN            (LVN_FIRST-55)
#define LVN_ITEMACTIVATE       (LVN_FIRST-14)
#define LVN_HOTTRACK           (LVN_FIRST-21)

// ── HDN_ITEMCHANGINGW (header control) ─────────────────────────
#define HDN_FIRST              (0U-300U)
#define HDN_ITEMCHANGINGW      (HDN_FIRST-20)
#define HDN_ITEMCHANGEDW       (HDN_FIRST-21)
#define HDN_ITEMCLICKW         (HDN_FIRST-22)
#define HDN_ITEMDBLCLICKW      (HDN_FIRST-23)

// ── CCM_SETUNICODEFORMAT (common control message) ──────────────
#define CCM_FIRST              0x2000
#define CCM_SETUNICODEFORMAT   (CCM_FIRST+5)
#define CCM_GETUNICODEFORMAT   (CCM_FIRST+6)

// ── ListView_* / TreeView_* macros ─────────────────────────────
#define ListView_GetItemCount(hwnd) \
    ((int)SendMessageW((hwnd), LVM_GETITEMCOUNT, 0, 0))
#define ListView_DeleteItem(hwnd, i) \
    ((BOOL)SendMessageW((hwnd), LVM_DELETEITEM, (WPARAM)(int)(i), 0))
#define ListView_DeleteAllItems(hwnd) \
    ((BOOL)SendMessageW((hwnd), LVM_DELETEALLITEMS, 0, 0))
#define ListView_EditLabel(hwnd, i) \
    ((HWND)SendMessageW((hwnd), 0x1023, (WPARAM)(int)(i), 0))
#define ListView_InsertColumn(hwnd, i, p) \
    ((int)SendMessageW((hwnd), LVM_INSERTCOLUMNW, (WPARAM)(int)(i), (LPARAM)(p)))
#define ListView_InsertColumnW(hwnd, i, p) ListView_InsertColumn(hwnd, i, p)
#define ListView_SetColumnWidth(hwnd, i, w) \
    ((BOOL)SendMessageW((hwnd), LVM_SETCOLUMNWIDTH, (WPARAM)(int)(i), MAKELPARAM((w), 0)))
#define ListView_SetExtendedListViewStyle(hwnd, s) \
    ((DWORD)SendMessageW((hwnd), LVM_SETEXTENDEDLISTVIEWSTYLE, 0, (LPARAM)(s)))
#define ListView_SortItems(hwnd, cmp, p) \
    ((BOOL)SendMessageW((hwnd), LVM_SORTITEMS, (WPARAM)(p), (LPARAM)(cmp)))
#define ListView_GetItemRect(hwnd, i, prc, code)                    \
    ((BOOL)((prc) ? ((((LPRECT)(prc))->left = (code)),              \
                     SendMessageW((hwnd), LVM_GETITEMRECT,          \
                                    (WPARAM)(int)(i), (LPARAM)(prc))) \
                  : FALSE))
#define ListView_GetSelectedCount(hwnd) \
    ((UINT)SendMessageW((hwnd), LVM_GETSELECTEDCOUNT, 0, 0))
#define ListView_GetNextItem(hwnd, i, flags) \
    ((int)SendMessageW((hwnd), LVM_GETNEXTITEM, (WPARAM)(int)(i), MAKELPARAM((flags), 0)))
#define ListView_FindItem(hwnd, i, plvfi) \
    ((int)SendMessageW((hwnd), 0x1053 /*LVM_FINDITEMW*/, (WPARAM)(int)(i), (LPARAM)(plvfi)))
#define ListView_GetItem(hwnd, p) \
    ((BOOL)SendMessageW((hwnd), LVM_GETITEMW, 0, (LPARAM)(p)))
#define ListView_SetItem(hwnd, p) \
    ((BOOL)SendMessageW((hwnd), LVM_SETITEMW, 0, (LPARAM)(p)))
#define ListView_EnsureVisible(hwnd, i, partial) \
    ((BOOL)SendMessageW((hwnd), LVM_ENSUREVISIBLE, (WPARAM)(int)(i), MAKELPARAM((BOOL)(partial), 0)))
#define ListView_SetItemState(hwnd, i, data, mask)                   \
    do {                                                              \
        LV_ITEM _ls = {0};                                            \
        _ls.stateMask = (mask);                                       \
        _ls.state     = (data);                                       \
        SendMessageW((hwnd), LVM_SETITEMSTATE,                        \
                     (WPARAM)(int)(i), (LPARAM)&_ls);                 \
    } while (0)
#define ListView_GetItemState(hwnd, i, mask) \
    ((UINT)SendMessageW((hwnd), LVM_GETITEMSTATE, (WPARAM)(int)(i), (LPARAM)(mask)))
#define ListView_SetColumn(hwnd, i, p) \
    ((int)SendMessageW((hwnd), LVM_SETCOLUMNW, (WPARAM)(int)(i), (LPARAM)(p)))
#define ListView_GetColumn(hwnd, i, p) \
    ((BOOL)SendMessageW((hwnd), LVM_GETCOLUMNW, (WPARAM)(int)(i), (LPARAM)(p)))
#define ListView_RedrawItems(hwnd, first, last) \
    ((BOOL)SendMessageW((hwnd), LVM_REDRAWITEMS, (WPARAM)(int)(first), (LPARAM)(int)(last)))
#define ListView_GetSelectionMark(hwnd) \
    ((int)SendMessageW((hwnd), LVM_GETSELECTIONMARK, 0, 0))
#define ListView_SetSelectionMark(hwnd, i) \
    ((int)SendMessageW((hwnd), LVM_SETSELECTIONMARK, 0, (LPARAM)(int)(i)))
#define ListView_SetItemCount(hwnd, n) \
    SendMessageW((hwnd), LVM_SETITEMCOUNT, (WPARAM)(int)(n), 0)
#define ListView_SetItemCountEx(hwnd, n, flags) \
    SendMessageW((hwnd), LVM_SETITEMCOUNT, (WPARAM)(int)(n), (LPARAM)(flags))
#define ListView_SetExtendedListViewStyleEx(hwnd, mask, style) \
    ((DWORD)SendMessageW((hwnd), LVM_SETEXTENDEDLISTVIEWSTYLE, (WPARAM)(mask), (LPARAM)(style)))
#define ListView_GetSubItemRect(hwnd, item, sub, code, prc)         \
    ((BOOL)((prc) ? ((((LPRECT)(prc))->top = (sub)),                \
                     (((LPRECT)(prc))->left = (code)),              \
                     SendMessageW((hwnd), LVM_GETSUBITEMRECT,       \
                                    (WPARAM)(int)(item),            \
                                    (LPARAM)(prc)))                 \
                  : FALSE))

// Notification + non-W aliases.
#define LVN_GETDISPINFO            LVN_GETDISPINFOW
#define LVN_BEGINLABELEDIT         LVN_BEGINLABELEDITW
#define LVN_ENDLABELEDIT           LVN_ENDLABELEDITW
#define HDN_ITEMCHANGING           HDN_ITEMCHANGINGW
#define HDN_ITEMCHANGED            HDN_ITEMCHANGEDW
#define HDN_ITEMCLICK              HDN_ITEMCLICKW
#define HDN_ITEMDBLCLICK           HDN_ITEMDBLCLICKW

// NMHEADERW for header-control notify.  HDITEMW already defined above.
typedef struct tagNMHEADERW {
    NMHDR   hdr;
    int     iItem;
    int     iButton;
    LPHDITEMW pitem;
} NMHEADERW, *LPNMHEADERW;
typedef NMHEADERW    NMHEADER;
typedef LPNMHEADERW  LPNMHEADER;

// LV image-list selectors.
#define LVSIL_NORMAL              0
#define LVSIL_SMALL               1
#define LVSIL_STATE               2
#define LVSIL_GROUPHEADER         3

// SNDMSG — abbrev. that legacy controls.h macros use.
#define SNDMSG SendMessageW

// LVM_GETHEADER + ListView_GetHeader.
#define LVM_GETHEADER             (LVM_FIRST + 31)
#define ListView_GetHeader(hwnd) \
    ((HWND)SendMessageW((hwnd), LVM_GETHEADER, 0, 0))

// SetWindowRedraw — wraps WM_SETREDRAW.
#define SetWindowRedraw(hwnd, redraw) \
    ((void)SendMessageW((hwnd), 0x000B /*WM_SETREDRAW*/, (WPARAM)(BOOL)(redraw), 0))

// NMLVKEYDOWN — list-view keydown notification.
typedef struct tagLVKEYDOWN {
    NMHDR hdr;
    WORD  wVKey;
    UINT  flags;
} NMLVKEYDOWN, *LPNMLVKEYDOWN;

// (NMTTDISPINFO aliases come after TOOLTIPTEXTW below.)

// LVHITTESTINFO + ListView_SubItemHitTest.
typedef struct tagLVHITTESTINFO {
    POINT pt;
    UINT  flags;
    int   iItem;
    int   iSubItem;
    int   iGroup;
} LVHITTESTINFO, *LPLVHITTESTINFO;
typedef LVHITTESTINFO LV_HITTESTINFO;
#define LVHT_NOWHERE              0x0001
#define LVHT_ONITEMICON           0x0002
#define LVHT_ONITEMLABEL          0x0004
#define LVHT_ONITEMSTATEICON      0x0008
#define LVHT_ABOVE                0x0010
#define LVHT_BELOW                0x0020
#define LVHT_TORIGHT              0x0040
#define LVHT_TOLEFT               0x0080
#define LVHT_ONITEM (LVHT_ONITEMICON | LVHT_ONITEMLABEL | LVHT_ONITEMSTATEICON)
#define ListView_HitTest(hwnd, p) \
    ((int)SendMessageW((hwnd), 0x1012 /*LVM_HITTEST*/, 0, (LPARAM)(p)))
#define ListView_SubItemHitTest(hwnd, p) \
    ((int)SendMessageW((hwnd), 0x1039 /*LVM_SUBITEMHITTEST*/, 0, (LPARAM)(p)))

// Tooltip notification + text struct.
typedef struct tagTOOLTIPTEXTW {
    NMHDR    hdr;
    LPWSTR   lpszText;
    wchar_t  szText[80];
    HINSTANCE hinst;
    UINT     uFlags;
    LPARAM   lParam;
} TOOLTIPTEXTW, *LPTOOLTIPTEXTW;
typedef TOOLTIPTEXTW TOOLTIPTEXT;
typedef LPTOOLTIPTEXTW LPTOOLTIPTEXT;
#define TTF_IDISHWND              0x0001
#define TTF_RTLREADING            0x0004
#define TTF_SUBCLASS              0x0010
#define TTF_TRACK                 0x0020
#define TTF_ABSOLUTE              0x0080
#define TTF_TRANSPARENT           0x0100
#define TTN_FIRST                 (0U-520U)
#define TTN_NEEDTEXTW             (TTN_FIRST-10)
#define TTN_GETDISPINFOW          (TTN_FIRST-10)
#define TTN_SHOW                  (TTN_FIRST-1)
#define TTN_POP                   (TTN_FIRST-2)
#define TTN_LINKCLICK             (TTN_FIRST-3)

// NMTTDISPINFO — tooltip dispinfo (mirror of TOOLTIPTEXT).
typedef TOOLTIPTEXTW NMTTDISPINFOW;
typedef LPTOOLTIPTEXTW LPNMTTDISPINFOW;
typedef NMTTDISPINFOW NMTTDISPINFO;
typedef LPNMTTDISPINFOW LPNMTTDISPINFO;

#define TreeView_GetSelection(hwnd) \
    ((HTREEITEM)SendMessageW((hwnd), TVM_GETNEXTITEM, TVGN_CARET, 0))
#define TreeView_SelectItem(hwnd, hitem) \
    ((BOOL)SendMessageW((hwnd), TVM_SELECTITEM, TVGN_CARET, (LPARAM)(HTREEITEM)(hitem)))
#define TreeView_GetItem(hwnd, p) \
    ((BOOL)SendMessageW((hwnd), TVM_GETITEMW, 0, (LPARAM)(p)))
#define TreeView_GetParent(hwnd, hitem) \
    ((HTREEITEM)SendMessageW((hwnd), TVM_GETNEXTITEM, TVGN_PARENT, (LPARAM)(HTREEITEM)(hitem)))

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // _COMMCTRL_H
