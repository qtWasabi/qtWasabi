// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#ifndef _WINUSER_H
#define _WINUSER_H
//
// winuser.h — Win32's windowing + message subsystem.  This shim
// provides the WM_* message constants, the SendMessage / PostMessage
// dispatcher entry points, and the minimum struct family
// (NMHDR, MSG, …) that gen_ml's WM_NOTIFY chain references.
//
// Dispatcher implementation routes through the wasabi-compat
// handle registry — see win32/handle-registry.h.
//

#include "basetsd.h"
#include "windef.h"

#ifdef __cplusplus
extern "C" {
#endif

// Win32 cosmetic macros / structs used by plugin code.
#define UNREFERENCED_PARAMETER(x) ((void)(x))

// MAKEINTRESOURCE — Win32 resource ID encoder (LOWORD = id).
#define MAKEINTRESOURCEA(i) ((LPSTR)((ULONG_PTR)((WORD)(i))))
#define MAKEINTRESOURCEW(i) ((LPWSTR)((ULONG_PTR)((WORD)(i))))
#if defined(UNICODE) || defined(_UNICODE)
#  define MAKEINTRESOURCE MAKEINTRESOURCEW
#else
#  define MAKEINTRESOURCE MAKEINTRESOURCEA
#endif

// Resource-type constants (passed to FindResource, LoadResource).
#define RT_CURSOR        MAKEINTRESOURCE(1)
#define RT_BITMAP        MAKEINTRESOURCE(2)
#define RT_ICON          MAKEINTRESOURCE(3)
#define RT_MENU          MAKEINTRESOURCE(4)
#define RT_DIALOG        MAKEINTRESOURCE(5)
#define RT_STRING        MAKEINTRESOURCE(6)
#define RT_RCDATA        MAKEINTRESOURCE(10)
#define RT_GROUP_CURSOR  MAKEINTRESOURCE(12)
#define RT_GROUP_ICON    MAKEINTRESOURCE(14)
#define RT_VERSION       MAKEINTRESOURCE(16)

// TrackPopupMenu params.  Opaque-struct stub.
typedef struct tagTPMPARAMS {
    UINT cbSize;
    RECT rcExclude;
} TPMPARAMS, *LPTPMPARAMS;

// WIN32_FIND_DATAW — file-enumeration struct.  ml_* uses it for
// directory walks.  Stubbed with the canonical Win32 layout so
// FindFirstFile-style code paths compile.
typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;

typedef struct _WIN32_FIND_DATAW {
    DWORD    dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD    nFileSizeHigh;
    DWORD    nFileSizeLow;
    DWORD    dwReserved0;
    DWORD    dwReserved1;
    WCHAR    cFileName[260];
    WCHAR    cAlternateFileName[14];
} WIN32_FIND_DATAW, *PWIN32_FIND_DATAW, *LPWIN32_FIND_DATAW;

typedef struct _WIN32_FIND_DATAA {
    DWORD    dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD    nFileSizeHigh;
    DWORD    nFileSizeLow;
    DWORD    dwReserved0;
    DWORD    dwReserved1;
    CHAR     cFileName[260];
    CHAR     cAlternateFileName[14];
} WIN32_FIND_DATAA, *PWIN32_FIND_DATAA, *LPWIN32_FIND_DATAA;

#if defined(UNICODE) || defined(_UNICODE)
typedef WIN32_FIND_DATAW WIN32_FIND_DATA;
typedef LPWIN32_FIND_DATAW LPWIN32_FIND_DATA;
#else
typedef WIN32_FIND_DATAA WIN32_FIND_DATA;
typedef LPWIN32_FIND_DATAA LPWIN32_FIND_DATA;
#endif

// GetModuleFileName — stub that returns the executable's path.
DWORD WINAPI GetModuleFileNameW(HINSTANCE /*hModule*/,
                                  LPWSTR lpFilename, DWORD nSize);
DWORD WINAPI GetModuleFileNameA(HINSTANCE /*hModule*/,
                                  LPSTR lpFilename, DWORD nSize);
#if defined(UNICODE) || defined(_UNICODE)
#  define GetModuleFileName GetModuleFileNameW
#else
#  define GetModuleFileName GetModuleFileNameA
#endif

// GetModuleHandle — returns HMODULE for the named module.  We
// always hand back null (we don't ship a module table); plugin
// code that branches on the result skips its "different module"
// path.
HMODULE WINAPI GetModuleHandleA(LPCSTR /*moduleName*/);
HMODULE WINAPI GetModuleHandleW(LPCWSTR /*moduleName*/);
#if defined(UNICODE) || defined(_UNICODE)
#  define GetModuleHandle GetModuleHandleW
#else
#  define GetModuleHandle GetModuleHandleA
#endif

// ── Menu API constants ─────────────────────────────────────────
// EnableMenuItem / CheckMenuItem / SetMenuDefaultItem flags.
#define MF_BYCOMMAND       0x00000000
#define MF_BYPOSITION      0x00000400
#define MF_ENABLED         0x00000000
#define MF_DISABLED        0x00000002
#define MF_GRAYED          0x00000001
#define MF_UNCHECKED       0x00000000
#define MF_CHECKED         0x00000008
#define MF_SEPARATOR       0x00000800
#define MF_STRING          0x00000000
#define MF_POPUP           0x00000010

BOOL    WINAPI EnableMenuItem(HMENU hMenu, UINT uIDEnableItem,
                                UINT uEnable);
BOOL    WINAPI SetMenuDefaultItem(HMENU hMenu, UINT uItem,
                                    UINT fByPos);
HMENU   WINAPI GetSubMenu(HMENU hMenu, int nPos);
HMENU   WINAPI CreatePopupMenu(void);
BOOL    WINAPI DestroyMenu(HMENU hMenu);
BOOL    WINAPI DeleteMenu(HMENU hMenu, UINT pos, UINT flags);
BOOL    WINAPI AppendMenuW(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem,
                             LPCWSTR lpNewItem);
BOOL    WINAPI AppendMenuA(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem,
                             LPCSTR lpNewItem);
#if defined(UNICODE) || defined(_UNICODE)
#  define AppendMenu AppendMenuW
#else
#  define AppendMenu AppendMenuA
#endif

// TrackPopupMenu flags.
#define TPM_LEFTBUTTON       0x0000
#define TPM_RIGHTBUTTON      0x0002
#define TPM_LEFTALIGN        0x0000
#define TPM_CENTERALIGN      0x0004
#define TPM_RIGHTALIGN       0x0008
#define TPM_TOPALIGN         0x0000
#define TPM_VCENTERALIGN     0x0010
#define TPM_BOTTOMALIGN      0x0020
#define TPM_NONOTIFY         0x0080
#define TPM_RETURNCMD        0x0100

// HWND special values.
#define HWND_DESKTOP         ((HWND)0)
#define HWND_BROADCAST       ((HWND)0xffff)
#define HWND_TOP             ((HWND)0)
#define HWND_BOTTOM          ((HWND)1)
#define HWND_TOPMOST         ((HWND)-1)
#define HWND_NOTOPMOST       ((HWND)-2)

// MAKEPOINTS / POINTSTOPOINT — DWORD ↔ POINTS conversion.
#define MAKEPOINTS(l) \
    (*(POINTS *)&(l))
#define POINTSTOPOINT(pt, pts) \
    do { (pt).x = (LONG)(pts).x; (pt).y = (LONG)(pts).y; } while (0)

// Window-property API.  Storage shared across all HWNDs in our
// registry — implementation in send-message.cpp.
HANDLE WINAPI GetPropW(HWND, LPCWSTR);
HANDLE WINAPI GetPropA(HWND, LPCSTR);
BOOL   WINAPI SetPropW(HWND, LPCWSTR, HANDLE);
BOOL   WINAPI SetPropA(HWND, LPCSTR, HANDLE);
HANDLE WINAPI RemovePropW(HWND, LPCWSTR);
HANDLE WINAPI RemovePropA(HWND, LPCSTR);
#if defined(UNICODE) || defined(_UNICODE)
#  define GetProp     GetPropW
#  define SetProp     SetPropW
#  define RemoveProp  RemovePropW
#else
#  define GetProp     GetPropA
#  define SetProp     SetPropA
#  define RemoveProp  RemovePropA
#endif

// Dynamic loading.  GetProcAddress always returns null (we don't
// load DLLs); LoadLibrary returns null.
typedef int (*FARPROC)();
FARPROC   WINAPI GetProcAddress(HMODULE, LPCSTR);
HMODULE   WINAPI LoadLibraryW(LPCWSTR);
HMODULE   WINAPI LoadLibraryA(LPCSTR);
BOOL      WINAPI FreeLibrary(HMODULE);
#if defined(UNICODE) || defined(_UNICODE)
#  define LoadLibrary LoadLibraryW
#else
#  define LoadLibrary LoadLibraryA
#endif

// Window coord mapping.  MapWindowPoints translates an array of
// POINT from src to dst.  Stub returns 0 (no points translated).
int WINAPI MapWindowPoints(HWND src, HWND dst, LPPOINT pts, UINT count);

// GetClassName — retrieves the window-class name for an HWND.
// Stub copies an empty string and returns 0.
int WINAPI GetClassNameA(HWND, LPSTR, int);
int WINAPI GetClassNameW(HWND, LPWSTR, int);
#if defined(UNICODE) || defined(_UNICODE)
#  define GetClassName GetClassNameW
#else
#  define GetClassName GetClassNameA
#endif

typedef struct tagCOPYDATASTRUCT {
    ULONG_PTR  dwData;
    DWORD      cbData;
    PVOID      lpData;
} COPYDATASTRUCT, *PCOPYDATASTRUCT;

// ── Common message constants ────────────────────────────────────
//
// Subset gen_ml + ml_* actually fire.  Numbering matches the Win32
// SDK so payloads ported from upstream code are wire-compatible.

#define WM_NULL                         0x0000
#define WM_CREATE                       0x0001
#define WM_DESTROY                      0x0002
#define WM_MOVE                         0x0003
#define WM_SIZE                         0x0005
#define WM_ACTIVATE                     0x0006
#define WM_SETFOCUS                     0x0007
#define WM_KILLFOCUS                    0x0008
#define WM_ENABLE                       0x000A
#define WM_SETREDRAW                    0x000B
#define WM_SETTEXT                      0x000C
#define WM_GETTEXT                      0x000D
#define WM_GETTEXTLENGTH                0x000E
#define WM_PAINT                        0x000F
#define WM_CLOSE                        0x0010
#define WM_QUIT                         0x0012
#define WM_ERASEBKGND                   0x0014
#define WM_SYSCOLORCHANGE               0x0015
#define WM_SHOWWINDOW                   0x0018
#define WM_SETTINGCHANGE                0x001A
#define WM_DEVMODECHANGE                0x001B
#define WM_ACTIVATEAPP                  0x001C
#define WM_FONTCHANGE                   0x001D
#define WM_TIMECHANGE                   0x001E
#define WM_CANCELMODE                   0x001F
#define WM_SETCURSOR                    0x0020
#define WM_MOUSEACTIVATE                0x0021
#define WM_CHILDACTIVATE                0x0022
#define WM_QUEUESYNC                    0x0023
#define WM_GETMINMAXINFO                0x0024
#define WM_DRAWITEM                     0x002B
#define WM_MEASUREITEM                  0x002C
#define WM_DELETEITEM                   0x002D
#define WM_VKEYTOITEM                   0x002E
#define WM_CHARTOITEM                   0x002F
#define WM_SETFONT                      0x0030
#define WM_GETFONT                      0x0031
#define WM_SETHOTKEY                    0x0032
#define WM_GETHOTKEY                    0x0033
#define WM_COMPAREITEM                  0x0039
#define WM_GETOBJECT                    0x003D
#define WM_COMPACTING                   0x0041
#define WM_WINDOWPOSCHANGING            0x0046
#define WM_WINDOWPOSCHANGED             0x0047
#define WM_COPYDATA                     0x004A
#define WM_NOTIFY                       0x004E
#define WM_INPUTLANGCHANGEREQUEST       0x0050
#define WM_INPUTLANGCHANGE              0x0051
#define WM_TCARD                        0x0052
#define WM_HELP                         0x0053
#define WM_USERCHANGED                  0x0054
#define WM_NOTIFYFORMAT                 0x0055
#define WM_CONTEXTMENU                  0x007B
#define WM_STYLECHANGING                0x007C
#define WM_STYLECHANGED                 0x007D
#define WM_DISPLAYCHANGE                0x007E
#define WM_GETICON                      0x007F
#define WM_SETICON                      0x0080
#define WM_NCCREATE                     0x0081
#define WM_NCDESTROY                    0x0082
#define WM_NCCALCSIZE                   0x0083
#define WM_NCHITTEST                    0x0084
#define WM_NCPAINT                      0x0085
#define WM_NCACTIVATE                   0x0086
#define WM_GETDLGCODE                   0x0087
#define WM_INITDIALOG                   0x0110
#define WM_COMMAND                      0x0111
#define WM_SYSCOMMAND                   0x0112
#define WM_TIMER                        0x0113
#define WM_HSCROLL                      0x0114
#define WM_VSCROLL                      0x0115
#define WM_INITMENU                     0x0116
#define WM_INITMENUPOPUP                0x0117
#define WM_MENUSELECT                   0x011F
#define WM_MENUCHAR                     0x0120
#define WM_ENTERIDLE                    0x0121
#define WM_CTLCOLORMSGBOX               0x0132
#define WM_CTLCOLOREDIT                 0x0133
#define WM_CTLCOLORLISTBOX              0x0134
#define WM_CTLCOLORBTN                  0x0135
#define WM_CTLCOLORDLG                  0x0136
#define WM_CTLCOLORSCROLLBAR            0x0137
#define WM_CTLCOLORSTATIC               0x0138

#define WM_KEYFIRST                     0x0100
#define WM_KEYDOWN                      0x0100
#define WM_KEYUP                        0x0101
#define WM_CHAR                         0x0102
#define WM_DEADCHAR                     0x0103
#define WM_SYSKEYDOWN                   0x0104
#define WM_SYSKEYUP                     0x0105
#define WM_SYSCHAR                      0x0106
#define WM_SYSDEADCHAR                  0x0107
#define WM_KEYLAST                      0x0108

#define WM_MOUSEFIRST                   0x0200
#define WM_MOUSEMOVE                    0x0200
#define WM_LBUTTONDOWN                  0x0201
#define WM_LBUTTONUP                    0x0202
#define WM_LBUTTONDBLCLK                0x0203
#define WM_RBUTTONDOWN                  0x0204
#define WM_RBUTTONUP                    0x0205
#define WM_RBUTTONDBLCLK                0x0206
#define WM_MBUTTONDOWN                  0x0207
#define WM_MBUTTONUP                    0x0208
#define WM_MBUTTONDBLCLK                0x0209
#define WM_MOUSEWHEEL                   0x020A
#define WM_MOUSELAST                    0x020E

#define WM_USER                         0x0400
#define WM_APP                          0x8000

// ── Notification & message structs ──────────────────────────────

typedef struct tagNMHDR {
    HWND        hwndFrom;
    UINT_PTR    idFrom;
    UINT        code;
} NMHDR, *LPNMHDR;

typedef struct tagMSG {
    HWND        hwnd;
    UINT        message;
    WPARAM      wParam;
    LPARAM      lParam;
    DWORD       time;
    POINT       pt;
    DWORD       lPrivate;
} MSG, *PMSG, *LPMSG;

typedef struct tagWINDOWPOS {
    HWND        hwnd;
    HWND        hwndInsertAfter;
    int         x;
    int         y;
    int         cx;
    int         cy;
    UINT        flags;
} WINDOWPOS, *LPWINDOWPOS, *PWINDOWPOS;

// Owner-draw control types (DRAWITEMSTRUCT.CtlType) + item states
// (DRAWITEMSTRUCT.itemState) — wa_dlg's WM_DRAWITEM button skinning.
#ifndef ODT_BUTTON
#define ODT_MENU      1
#define ODT_LISTBOX   2
#define ODT_COMBOBOX  3
#define ODT_BUTTON    4
#define ODT_STATIC    5
#endif
#ifndef ODS_SELECTED
#define ODS_SELECTED  0x0001
#define ODS_DISABLED  0x0004
#define ODS_FOCUS     0x0010
#endif

typedef struct tagDRAWITEMSTRUCT {
    UINT        CtlType;
    UINT        CtlID;
    UINT        itemID;
    UINT        itemAction;
    UINT        itemState;
    HWND        hwndItem;
    HDC         hDC;
    RECT        rcItem;
    ULONG_PTR   itemData;
} DRAWITEMSTRUCT, *LPDRAWITEMSTRUCT;

typedef struct tagMEASUREITEMSTRUCT {
    UINT        CtlType;
    UINT        CtlID;
    UINT        itemID;
    UINT        itemWidth;
    UINT        itemHeight;
    ULONG_PTR   itemData;
} MEASUREITEMSTRUCT, *LPMEASUREITEMSTRUCT;

// ── WndProc signature ──────────────────────────────────────────
typedef LRESULT (CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef LRESULT (CALLBACK *DLGPROC)(HWND, UINT, WPARAM, LPARAM);

// ── Message dispatch entry points ──────────────────────────────
//
// These are the Win32 API surfaces gen_ml uses to drive messages
// through the windowing system.  Implementation lives in
// win32/send-message.cpp.

LRESULT WINAPI SendMessageW    (HWND, UINT, WPARAM, LPARAM);
LRESULT WINAPI SendMessageA    (HWND, UINT, WPARAM, LPARAM);
LRESULT WINAPI SendDlgItemMessageW(HWND, int, UINT, WPARAM, LPARAM);
LRESULT WINAPI SendDlgItemMessageA(HWND, int, UINT, WPARAM, LPARAM);
BOOL    WINAPI PostMessageW    (HWND, UINT, WPARAM, LPARAM);
BOOL    WINAPI PostMessageA    (HWND, UINT, WPARAM, LPARAM);
LRESULT WINAPI DefWindowProcW  (HWND, UINT, WPARAM, LPARAM);
LRESULT WINAPI DefWindowProcA  (HWND, UINT, WPARAM, LPARAM);
LRESULT WINAPI CallWindowProcW (WNDPROC, HWND, UINT, WPARAM, LPARAM);
LRESULT WINAPI CallWindowProcA (WNDPROC, HWND, UINT, WPARAM, LPARAM);

// Per Win32 conventions, the W (wide) and A (ansi) variants take
// the same args and dispatch the same way for us — message
// payloads that carry text encoding propagate as WPARAM/LPARAM
// already.  When a real encoding gap matters (WM_SETTEXT et al.)
// the WindowObject's wndProc handles it.

// TCHAR-aware aliasing (Wasabi historically built with UNICODE).
#if defined(UNICODE) || defined(_UNICODE)
#  define SendMessage         SendMessageW
#  define SendDlgItemMessage  SendDlgItemMessageW
#  define PostMessage         PostMessageW
#  define DefWindowProc       DefWindowProcW
#  define CallWindowProc      CallWindowProcW
#else
#  define SendMessage         SendMessageA
#  define SendDlgItemMessage  SendDlgItemMessageA
#  define PostMessage         PostMessageA
#  define DefWindowProc       DefWindowProcA
#  define CallWindowProc      CallWindowProcA
#endif

// ── Misc helpers gen_ml's window code calls ──────────
HWND     WINAPI GetDlgItem        (HWND, int);
BOOL     WINAPI IsWindow          (HWND);
BOOL     WINAPI IsWindowVisible   (HWND);
BOOL     WINAPI IsWindowEnabled   (HWND);
HWND     WINAPI GetForegroundWindow(void);
LPWSTR   WINAPI CharUpperW        (LPWSTR);

// CREATESTRUCT / NCCALCSIZE_PARAMS — referenced by the Winamp main-
// window machinery the playlist code pulls in.  Layout-only here.
#ifndef _CREATESTRUCT_DEFINED
#define _CREATESTRUCT_DEFINED 1
typedef struct tagCREATESTRUCTW {
    LPVOID    lpCreateParams;
    HINSTANCE hInstance;
    HMENU     hMenu;
    HWND      hwndParent;
    int       cy, cx, y, x;
    LONG      style;
    LPCWSTR   lpszName;
    LPCWSTR   lpszClass;
    DWORD     dwExStyle;
} CREATESTRUCTW, *LPCREATESTRUCTW;
typedef CREATESTRUCTW  CREATESTRUCT;
typedef LPCREATESTRUCTW LPCREATESTRUCT;
typedef struct tagNCCALCSIZE_PARAMS {
    RECT       rgrc[3];
    PWINDOWPOS lppos;
} NCCALCSIZE_PARAMS, *LPNCCALCSIZE_PARAMS;
#endif
BOOL     WINAPI ShowWindow        (HWND, int);
BOOL     WINAPI DestroyWindow     (HWND);
HWND     WINAPI GetParent         (HWND);
HWND     WINAPI SetParent         (HWND, HWND);
LONG_PTR WINAPI GetWindowLongPtrW (HWND, int);
LONG_PTR WINAPI SetWindowLongPtrW (HWND, int, LONG_PTR);
LONG_PTR WINAPI GetWindowLongPtrA (HWND, int);
LONG_PTR WINAPI SetWindowLongPtrA (HWND, int, LONG_PTR);
BOOL     WINAPI GetClientRect     (HWND, LPRECT);
BOOL     WINAPI GetWindowRect     (HWND, LPRECT);
BOOL     WINAPI MoveWindow        (HWND, int, int, int, int, BOOL);

// ShowWindow constants — gen_ml uses SW_SHOW / SW_HIDE most.
#define SW_HIDE              0
#define SW_SHOWNORMAL        1
#define SW_SHOWMINIMIZED     2
#define SW_SHOWMAXIMIZED     3
#define SW_SHOWNOACTIVATE    4
#define SW_SHOW              5
#define SW_MINIMIZE          6
#define SW_SHOWMINNOACTIVE   7
#define SW_SHOWNA            8
#define SW_RESTORE           9
#define SW_SHOWDEFAULT       10
#define SW_FORCEMINIMIZE     11

// GetWindowLong indices — only GWL_USERDATA / GWL_WNDPROC actually
// matter for gen_ml, but ship the canonical set.
#define GWL_WNDPROC          (-4)
#define GWL_HINSTANCE        (-6)
#define GWL_HWNDPARENT       (-8)
#define GWL_STYLE            (-16)
#define GWL_EXSTYLE          (-20)
#define GWL_USERDATA         (-21)
#define GWL_ID               (-12)
#define GWLP_WNDPROC         GWL_WNDPROC
#define GWLP_USERDATA        GWL_USERDATA
#define GWLP_ID              GWL_ID

// SetWindowLongPtr / GetWindowLongPtr — Win32 SDK provides these
// as UNICODE-aliased macros.  Plugin code calls the non-suffixed
// form expecting the macro to expand.
#if defined(UNICODE) || defined(_UNICODE)
#  define SetWindowLongPtr SetWindowLongPtrW
#  define GetWindowLongPtr GetWindowLongPtrW
#else
#  define SetWindowLongPtr SetWindowLongPtrA
#  define GetWindowLongPtr GetWindowLongPtrA
#endif

// DWLP_* — dialog-proc window-long indices (offset by sizeof(LRESULT)
// from the GWLP set, so Win32 SDK calls these out explicitly).
#define DWLP_MSGRESULT       0
#define DWLP_DLGPROC         (DWLP_MSGRESULT + sizeof(LRESULT))
#define DWLP_USER            (DWLP_DLGPROC   + sizeof(void *))

// ── Dialog API surface (ml_playlists) ─────────────────
// Dialog return codes (also used by MessageBox).
#define IDOK                 1
#define IDCANCEL             2
#define IDABORT              3
#define IDRETRY              4
#define IDIGNORE             5
#define IDYES                6
#define IDNO                 7
#define IDCLOSE              8
#define IDHELP               9
#define IDTRYAGAIN           10
#define IDCONTINUE           11

// Button-state constants for IsDlgButtonChecked / CheckDlgButton.
#define BST_UNCHECKED        0x0000
#define BST_CHECKED          0x0001
#define BST_INDETERMINATE    0x0002
#define BST_PUSHED           0x0004
#define BST_FOCUS            0x0008

// MessageBox flags.
#define MB_OK                0x00000000
#define MB_OKCANCEL          0x00000001
#define MB_ABORTRETRYIGNORE  0x00000002
#define MB_YESNOCANCEL       0x00000003
#define MB_YESNO             0x00000004
#define MB_RETRYCANCEL       0x00000005
#define MB_ICONHAND          0x00000010
#define MB_ICONQUESTION      0x00000020
#define MB_ICONEXCLAMATION   0x00000030
#define MB_ICONASTERISK      0x00000040
#define MB_ICONINFORMATION   MB_ICONASTERISK
#define MB_ICONWARNING       MB_ICONEXCLAMATION
#define MB_ICONERROR         MB_ICONHAND
#define MB_DEFBUTTON1        0x00000000
#define MB_DEFBUTTON2        0x00000100
#define MB_DEFBUTTON3        0x00000200
#define MB_APPLMODAL         0x00000000

int      WINAPI MessageBoxW(HWND, LPCWSTR text, LPCWSTR title, UINT type);
int      WINAPI MessageBoxA(HWND, LPCSTR  text, LPCSTR  title, UINT type);
#if defined(UNICODE) || defined(_UNICODE)
#  define MessageBox MessageBoxW
#else
#  define MessageBox MessageBoxA
#endif

BOOL     WINAPI EndDialog          (HWND, INT_PTR);
UINT     WINAPI GetDlgItemTextW    (HWND, int, LPWSTR, int);
UINT     WINAPI GetDlgItemTextA    (HWND, int, LPSTR,  int);
BOOL     WINAPI SetDlgItemTextW    (HWND, int, LPCWSTR);
BOOL     WINAPI SetDlgItemTextA    (HWND, int, LPCSTR);
UINT     WINAPI GetDlgItemInt      (HWND, int, BOOL *, BOOL);
BOOL     WINAPI SetDlgItemInt      (HWND, int, UINT, BOOL);
BOOL     WINAPI IsDlgButtonChecked (HWND, int);
BOOL     WINAPI CheckDlgButton     (HWND, int, UINT);
#if defined(UNICODE) || defined(_UNICODE)
#  define GetDlgItemText GetDlgItemTextW
#  define SetDlgItemText SetDlgItemTextW
#else
#  define GetDlgItemText GetDlgItemTextA
#  define SetDlgItemText SetDlgItemTextA
#endif

// MENUITEMINFOW — ml_playlists builds these and passes to
// SetMenuItemInfoW / GetMenuItemInfoW.  Field layout matches Win32.
typedef struct tagMENUITEMINFOW {
    UINT      cbSize;
    UINT      fMask;
    UINT      fType;
    UINT      fState;
    UINT      wID;
    HMENU     hSubMenu;
    HBITMAP   hbmpChecked;
    HBITMAP   hbmpUnchecked;
    ULONG_PTR dwItemData;
    LPWSTR    dwTypeData;
    UINT      cch;
    HBITMAP   hbmpItem;
} MENUITEMINFOW, *LPMENUITEMINFOW;
typedef MENUITEMINFOW MENUITEMINFO;
typedef LPMENUITEMINFOW LPMENUITEMINFO;
BOOL     WINAPI CheckRadioButton   (HWND, int, int, int);
BOOL     WINAPI EnableWindow       (HWND, BOOL);
BOOL     WINAPI SetWindowTextW     (HWND, LPCWSTR);
BOOL     WINAPI SetWindowTextA     (HWND, LPCSTR);
int      WINAPI GetWindowTextW     (HWND, LPWSTR, int);
int      WINAPI GetWindowTextA     (HWND, LPSTR, int);
LRESULT  WINAPI DefWindowProcW     (HWND, UINT, WPARAM, LPARAM);
LRESULT  WINAPI DefWindowProcA     (HWND, UINT, WPARAM, LPARAM);
HWND     WINAPI FindWindowExW      (HWND, HWND, LPCWSTR, LPCWSTR);
HWND     WINAPI FindWindowExA      (HWND, HWND, LPCSTR,  LPCSTR);
HWND     WINAPI FindWindowW        (LPCWSTR, LPCWSTR);
HWND     WINAPI FindWindowA        (LPCSTR,  LPCSTR);
BOOL     WINAPI EnumChildWindows   (HWND, BOOL (CALLBACK *)(HWND, LPARAM), LPARAM);
BOOL     WINAPI SetWindowPos       (HWND, HWND, int, int, int, int, UINT);
BOOL     WINAPI ScreenToClient     (HWND, LPPOINT);
BOOL     WINAPI ClientToScreen     (HWND, LPPOINT);
BOOL     WINAPI InvalidateRect     (HWND, const RECT *, BOOL);
BOOL     WINAPI UpdateWindow       (HWND);
HWND     WINAPI SetFocus           (HWND);
HWND     WINAPI GetFocus           (void);
UINT_PTR WINAPI SetTimer           (HWND, UINT_PTR, UINT, void *);
BOOL     WINAPI KillTimer          (HWND, UINT_PTR);
DWORD    WINAPI GetTickCount       (void);
SHORT    WINAPI GetAsyncKeyState   (int);
SHORT    WINAPI GetKeyState        (int);
HWND     WINAPI SetCapture         (HWND);
BOOL     WINAPI ReleaseCapture     (void);
HWND     WINAPI GetCapture         (void);
BOOL     WINAPI GetCursorPos       (LPPOINT);
BOOL     WINAPI SetCursorPos       (int, int);
BOOL     WINAPI CopyRect           (LPRECT, const RECT *);
BOOL     WINAPI EqualRect          (const RECT *, const RECT *);
BOOL     WINAPI InflateRect        (LPRECT, int, int);
BOOL     WINAPI IntersectRect      (LPRECT, const RECT *, const RECT *);
BOOL     WINAPI OffsetRect         (LPRECT, int, int);
BOOL     WINAPI PtInRect           (const RECT *, POINT);
BOOL     WINAPI SetRect            (LPRECT, int, int, int, int);
BOOL     WINAPI SetRectEmpty       (LPRECT);
BOOL     WINAPI UnionRect          (LPRECT, const RECT *, const RECT *);
BOOL     WINAPI DrawFocusRect      (HDC, const RECT *);

// Virtual-key constants — only ones ml_playlists uses.
#define VK_SHIFT             0x10
#define VK_CONTROL           0x11
#define VK_MENU              0x12
#define VK_LBUTTON           0x01
#define VK_RBUTTON           0x02
#define VK_BACK              0x08
#define VK_TAB               0x09
#define VK_RETURN            0x0D
#define VK_ESCAPE            0x1B
#define VK_SPACE             0x20
#define VK_LEFT              0x25
#define VK_UP                0x26
#define VK_RIGHT             0x27
#define VK_DOWN              0x28
#define VK_DELETE            0x2E
#define VK_F1                0x70
#define VK_F2                0x71
#define VK_F3                0x72
#define VK_F4                0x73
#define VK_F5                0x74
#define VK_F6                0x75
#define VK_F7                0x76
#define VK_F8                0x77
#define VK_F9                0x78
#define VK_F10               0x79
#define VK_F11               0x7A
#define VK_F12               0x7B
#define VK_INSERT            0x2D
#define VK_HOME              0x24
#define VK_END               0x23
#define VK_PRIOR             0x21
#define VK_NEXT              0x22
#define VK_PAUSE             0x13
#define VK_CAPITAL           0x14

// Sleep + PeekMessage — message-loop primitives.  MSG already
// defined above.
void   WINAPI Sleep         (DWORD ms);
BOOL   WINAPI PeekMessageW  (LPMSG, HWND, UINT, UINT, UINT);
BOOL   WINAPI PeekMessageA  (LPMSG, HWND, UINT, UINT, UINT);
BOOL   WINAPI GetMessageW   (LPMSG, HWND, UINT, UINT);
LONG   WINAPI DispatchMessageW(const MSG *);
BOOL   WINAPI TranslateMessage(const MSG *);
#define PM_NOREMOVE     0x0000
#define PM_REMOVE       0x0001
#define PM_NOYIELD      0x0002
#if defined(UNICODE) || defined(_UNICODE)
#  define PeekMessage  PeekMessageW
#  define GetMessage   GetMessageW
#  define DispatchMessage DispatchMessageW
#endif

// WC_NO_BEST_FIT_CHARS — WideCharToMultiByte flag.
#ifndef WC_NO_BEST_FIT_CHARS
#  define WC_NO_BEST_FIT_CHARS 0x00000400
#endif

// GetShortPathNameW — legacy Win32 short-name path API.  Linux has
// no equivalent; pass through unchanged.
DWORD WINAPI GetShortPathNameW(LPCWSTR longPath, LPWSTR shortPath, DWORD sz);

// ACCEL + accelerator table — Win32 keyboard-accelerator binding.
typedef struct tagACCEL {
    BYTE  fVirt;
    WORD  key;
    WORD  cmd;
} ACCEL, *LPACCEL;
HACCEL WINAPI CreateAcceleratorTableW(LPACCEL, int);
int    WINAPI CopyAcceleratorTableW  (HACCEL, LPACCEL, int);
int    WINAPI CopyAcceleratorTable   (HACCEL, LPACCEL, int);
BOOL   WINAPI DestroyAcceleratorTable(HACCEL);
int    WINAPI TranslateAcceleratorW  (HWND, HACCEL, LPMSG);

// Edit control messages.
#define EM_SETSEL            0xB1
#define EM_GETSEL            0xB0
#define EM_REPLACESEL        0xC2

// ComboBox messages.
#define CB_GETEDITSEL        0x0140
#define CB_LIMITTEXT         0x0141
#define CB_SETEDITSEL        0x0142
#define CB_ADDSTRING         0x0143
#define CB_DELETESTRING      0x0144
#define CB_DIR               0x0145
#define CB_GETCOUNT          0x0146
#define CB_GETCURSEL         0x0147
#define CB_GETLBTEXT         0x0148
#define CB_GETLBTEXTLEN      0x0149
#define CB_INSERTSTRING      0x014A
#define CB_RESETCONTENT      0x014B
#define CB_FINDSTRING        0x014C
#define CB_SELECTSTRING      0x014D
#define CB_SETCURSEL         0x014E
#define CB_SHOWDROPDOWN      0x014F
#define CB_GETITEMDATA       0x0150
#define CB_SETITEMDATA       0x0151
#define CB_GETDROPPEDCONTROLRECT 0x0152
#define CB_SETITEMHEIGHT     0x0153
#define CB_GETITEMHEIGHT     0x0154
#define CB_ERR               (-1)
#define CB_ERRSPACE          (-2)

// ListBox messages.
#define LB_ADDSTRING         0x0180
#define LB_INSERTSTRING      0x0181
#define LB_DELETESTRING      0x0182
#define LB_RESETCONTENT      0x0184
#define LB_SETSEL            0x0185
#define LB_SETCURSEL         0x0186
#define LB_GETSEL            0x0187
#define LB_GETCURSEL         0x0188
#define LB_GETTEXT           0x0189
#define LB_GETTEXTLEN        0x018A
#define LB_GETCOUNT          0x018B
#define LB_ERR               (-1)

// File-find API.
typedef HANDLE HFINDFILE;
HANDLE WINAPI FindFirstFileW(LPCWSTR, void *);
HANDLE WINAPI FindFirstFileA(LPCSTR,  void *);
BOOL   WINAPI FindNextFileW (HANDLE, void *);
BOOL   WINAPI FindNextFileA (HANDLE, void *);
BOOL   WINAPI FindClose     (HANDLE);
#if defined(UNICODE) || defined(_UNICODE)
#  define FindFirstFile FindFirstFileW
#  define FindNextFile  FindNextFileW
#endif

#if defined(UNICODE) || defined(_UNICODE)
#  define DefWindowProc  DefWindowProcW
#  define FindWindow     FindWindowW
#  define FindWindowEx   FindWindowExW
#  define SetWindowText  SetWindowTextW
#  define GetWindowText  GetWindowTextW
#else
#  define DefWindowProc  DefWindowProcA
#  define FindWindow     FindWindowA
#  define FindWindowEx   FindWindowExA
#endif

// SetWindowPos flags.
#define SWP_NOSIZE           0x0001
#define SWP_NOMOVE           0x0002
#define SWP_NOZORDER         0x0004
#define SWP_NOREDRAW         0x0008
#define SWP_NOACTIVATE       0x0010
#define SWP_FRAMECHANGED     0x0020
#define SWP_SHOWWINDOW       0x0040
#define SWP_HIDEWINDOW       0x0080
#define SWP_NOCOPYBITS       0x0100
#define SWP_NOOWNERZORDER    0x0200
#define SWP_NOSENDCHANGING   0x0400
#define SWF_NORESIZE         SWP_NOSIZE

// Dialog-related WM_* messages.
#define WM_NCACTIVATE        0x0086
#define WM_NEXTDLGCTL        0x0028
#define WM_GETDLGCODE        0x0087
#define WM_HSCROLL           0x0114
#define WM_VSCROLL           0x0115
#define WM_INITDIALOG        0x0110
#define WM_KEYDOWN           0x0100
#define WM_KEYUP             0x0101
#define WM_CHAR              0x0102
#define WM_LBUTTONDOWN       0x0201
#define WM_LBUTTONUP         0x0202
#define WM_RBUTTONDOWN       0x0204
#define WM_RBUTTONUP         0x0205
#define WM_CONTEXTMENU       0x007B
#define WM_DRAWITEM          0x002B
#define WM_MEASUREITEM       0x002C

// ── Menu API extension ───────────────────────────────────────
HMENU    WINAPI CreateMenu      (void);
BOOL     WINAPI InsertMenuItemW (HMENU, UINT, BOOL, void *);
BOOL     WINAPI InsertMenuW     (HMENU, UINT, UINT, UINT_PTR, LPCWSTR);
BOOL     WINAPI InsertMenuA     (HMENU, UINT, UINT, UINT_PTR, LPCSTR);
BOOL     WINAPI RemoveMenu      (HMENU, UINT, UINT);
BOOL     WINAPI CheckMenuItem   (HMENU, UINT, UINT);
BOOL     WINAPI CheckMenuRadioItem(HMENU, UINT, UINT, UINT, UINT);
BOOL     WINAPI GetMenuItemInfoW(HMENU, UINT, BOOL, void *);
BOOL     WINAPI SetMenuItemInfoW(HMENU, UINT, BOOL, void *);
int      WINAPI GetMenuItemCount(HMENU);
UINT     WINAPI GetMenuItemID   (HMENU, int);
HMENU    WINAPI LoadMenuW       (HINSTANCE, LPCWSTR);

#define MIIM_STATE           0x00000001
#define MIIM_ID              0x00000002
#define MIIM_SUBMENU         0x00000004
#define MIIM_CHECKMARKS      0x00000008
#define MIIM_TYPE            0x00000010
#define MIIM_DATA            0x00000020
#define MIIM_STRING          0x00000040
#define MIIM_BITMAP          0x00000080
#define MIIM_FTYPE           0x00000100
#define MFT_STRING           0x00000000
#define MFT_BITMAP           0x00000004
#define MFT_OWNERDRAW        0x00000100
#define MFT_RADIOCHECK       0x00000200
#define MFT_SEPARATOR        0x00000800
#define MFS_DEFAULT          0x00001000
#define MFS_GRAYED           0x00000003
#define MFS_DISABLED         0x00000003
#define MFS_CHECKED          0x00000008
#define MFS_HILITE           0x00000080
#define MFS_ENABLED          0x00000000
#define MFS_UNCHECKED        0x00000000
#define MFS_UNHILITE         0x00000000

// ── Memory API ───────────────────────────────────────────────
HGLOBAL  WINAPI GlobalAlloc       (UINT, SIZE_T);
HGLOBAL  WINAPI GlobalFree        (HGLOBAL);
HGLOBAL  WINAPI GlobalReAlloc     (HGLOBAL, SIZE_T, UINT);
LPVOID   WINAPI GlobalLock        (HGLOBAL);
BOOL     WINAPI GlobalUnlock      (HGLOBAL);
SIZE_T   WINAPI GlobalSize        (HGLOBAL);
#define GMEM_FIXED               0x0000
#define GMEM_MOVEABLE            0x0002
#define GMEM_ZEROINIT            0x0040
#define GPTR                     (GMEM_FIXED | GMEM_ZEROINIT)
#define GHND                     (GMEM_MOVEABLE | GMEM_ZEROINIT)
#define GMEM_SHARE               0x2000

// ── File I/O ─────────────────────────────────────────────────
HANDLE   WINAPI CreateFileW       (LPCWSTR, DWORD, DWORD, void *,
                                    DWORD, DWORD, HANDLE);
HANDLE   WINAPI CreateFileA       (LPCSTR,  DWORD, DWORD, void *,
                                    DWORD, DWORD, HANDLE);
BOOL     WINAPI CloseHandle       (HANDLE);
BOOL     WINAPI CopyFileW         (LPCWSTR, LPCWSTR, BOOL);
BOOL     WINAPI CopyFileA         (LPCSTR,  LPCSTR,  BOOL);
BOOL     WINAPI MoveFileW         (LPCWSTR, LPCWSTR);
BOOL     WINAPI DeleteFileW       (LPCWSTR);
BOOL     WINAPI DeleteFileA       (LPCSTR);
UINT     WINAPI GetTempFileNameW  (LPCWSTR, LPCWSTR, UINT, LPWSTR);
DWORD    WINAPI GetTempPathW      (DWORD, LPWSTR);
DWORD    WINAPI GetCurrentDirectoryW(DWORD, LPWSTR);
BOOL     WINAPI SetCurrentDirectoryW(LPCWSTR);
DWORD    WINAPI GetCurrentDirectoryA(DWORD, LPSTR);
BOOL     WINAPI SetCurrentDirectoryA(LPCSTR);
HCURSOR  WINAPI LoadCursorW         (HINSTANCE, LPCWSTR);
HCURSOR  WINAPI LoadCursorA         (HINSTANCE, LPCSTR);
HICON    WINAPI LoadIconW           (HINSTANCE, LPCWSTR);
DWORD    WINAPI GetFileSize       (HANDLE, LPDWORD);
BOOL     WINAPI ReadFile          (HANDLE, LPVOID, DWORD, LPDWORD, void *);
BOOL     WINAPI WriteFile         (HANDLE, LPCVOID, DWORD, LPDWORD, void *);
DWORD    WINAPI SetFilePointer    (HANDLE, LONG, PLONG, DWORD);
DWORD    WINAPI GetFileAttributesW(LPCWSTR);
BOOL     WINAPI SetFileAttributesW(LPCWSTR, DWORD);

#if defined(UNICODE) || defined(_UNICODE)
#  define CreateFile             CreateFileW
#  define CopyFile               CopyFileW
#  define DeleteFile             DeleteFileW
#  define GetTempFileName        GetTempFileNameW
#  define GetTempPath            GetTempPathW
#  define GetFileAttributes      GetFileAttributesW
#  define SetFileAttributes      SetFileAttributesW
#  define GetCurrentDirectory    GetCurrentDirectoryW
#  define SetCurrentDirectory    SetCurrentDirectoryW
#  define LoadCursor             LoadCursorW
#  define LoadIcon               LoadIconW
#  define InsertMenu             InsertMenuW
#  define InsertMenuItem         InsertMenuItemW
#  define GetMenuItemInfo        GetMenuItemInfoW
#  define SetMenuItemInfo        SetMenuItemInfoW
#  define LoadMenu               LoadMenuW
#else
#  define CreateFile             CreateFileA
#  define CopyFile               CopyFileA
#  define DeleteFile             DeleteFileA
#  define GetCurrentDirectory    GetCurrentDirectoryA
#  define SetCurrentDirectory    SetCurrentDirectoryA
#  define LoadCursor             LoadCursorA
#  define InsertMenu             InsertMenuA
#endif

// CreateFile access flags.
#define GENERIC_READ         0x80000000
#define GENERIC_WRITE        0x40000000
#define GENERIC_EXECUTE      0x20000000
#define GENERIC_ALL          0x10000000
#define FILE_SHARE_READ      0x00000001
#define FILE_SHARE_WRITE     0x00000002
#define FILE_SHARE_DELETE    0x00000004
#define CREATE_NEW           1
#define CREATE_ALWAYS        2
#define OPEN_EXISTING        3
#define OPEN_ALWAYS          4
#define TRUNCATE_EXISTING    5
#define FILE_ATTRIBUTE_READONLY        0x00000001
#define FILE_ATTRIBUTE_HIDDEN          0x00000002
#define FILE_ATTRIBUTE_DIRECTORY       0x00000010
#define FILE_ATTRIBUTE_NORMAL          0x00000080
#define INVALID_FILE_ATTRIBUTES        ((DWORD)-1)
#define FILE_BEGIN           0
#define FILE_CURRENT         1
#define FILE_END             2

// ── Profile / INI API (legacy Win32) ─────────────────────────
UINT     WINAPI GetPrivateProfileIntA   (LPCSTR  app, LPCSTR  key,
                                            INT def, LPCSTR  file);
UINT     WINAPI GetPrivateProfileIntW   (LPCWSTR app, LPCWSTR key,
                                            INT def, LPCWSTR file);
DWORD    WINAPI GetPrivateProfileStringA(LPCSTR  app, LPCSTR  key,
                                            LPCSTR  def, LPSTR  out,
                                            DWORD sz, LPCSTR file);
DWORD    WINAPI GetPrivateProfileStringW(LPCWSTR app, LPCWSTR key,
                                            LPCWSTR def, LPWSTR out,
                                            DWORD sz, LPCWSTR file);
BOOL     WINAPI WritePrivateProfileStringA(LPCSTR  app, LPCSTR  key,
                                              LPCSTR  val, LPCSTR  file);
BOOL     WINAPI WritePrivateProfileStringW(LPCWSTR app, LPCWSTR key,
                                              LPCWSTR val, LPCWSTR file);

// ── Common-dialog OPENFILENAMEW ──────────────────────────────
typedef struct tagOFNW {
    DWORD     lStructSize;
    HWND      hwndOwner;
    HINSTANCE hInstance;
    LPCWSTR   lpstrFilter;
    LPWSTR    lpstrCustomFilter;
    DWORD     nMaxCustFilter;
    DWORD     nFilterIndex;
    LPWSTR    lpstrFile;
    DWORD     nMaxFile;
    LPWSTR    lpstrFileTitle;
    DWORD     nMaxFileTitle;
    LPCWSTR   lpstrInitialDir;
    LPCWSTR   lpstrTitle;
    DWORD     Flags;
    WORD      nFileOffset;
    WORD      nFileExtension;
    LPCWSTR   lpstrDefExt;
    LPARAM    lCustData;
    void     *lpfnHook;
    LPCWSTR   lpTemplateName;
    void     *pvReserved;
    DWORD     dwReserved;
    DWORD     FlagsEx;
} OPENFILENAMEW, *LPOPENFILENAMEW;
typedef OPENFILENAMEW OPENFILENAME;

BOOL     WINAPI GetOpenFileNameW(LPOPENFILENAMEW);
BOOL     WINAPI GetSaveFileNameW(LPOPENFILENAMEW);

#define OFN_READONLY                 0x00000001
#define OFN_OVERWRITEPROMPT          0x00000002
#define OFN_HIDEREADONLY             0x00000004
#define OFN_NOCHANGEDIR              0x00000008
#define OFN_SHOWHELP                 0x00000010
#define OFN_NOVALIDATE               0x00000100
#define OFN_ALLOWMULTISELECT         0x00000200
#define OFN_EXTENSIONDIFFERENT       0x00000400
#define OFN_PATHMUSTEXIST            0x00000800
#define OFN_FILEMUSTEXIST            0x00001000
#define OFN_CREATEPROMPT             0x00002000
#define OFN_SHAREAWARE               0x00004000
#define OFN_NOREADONLYRETURN         0x00008000
#define OFN_NOTESTFILECREATE         0x00010000
#define OFN_NONETWORKBUTTON          0x00020000
#define OFN_NOLONGNAMES              0x00040000
#define OFN_EXPLORER                 0x00080000
#define OFN_NODEREFERENCELINKS       0x00100000
#define OFN_LONGNAMES                0x00200000
#define OFN_ENABLEHOOK               0x00000020
#define OFN_ENABLETEMPLATE           0x00000040
#define OFN_ENABLETEMPLATEHANDLE     0x00000080
#define OFN_ENABLESIZING             0x00800000
#define OFN_ENABLEINCLUDENOTIFY      0x00400000

DWORD WINAPI CommDlgExtendedError(void);

// CommDlg error codes.
#define CDERR_DIALOGFAILURE         0xFFFF
#define CDERR_GENERALCODES          0x0000
#define CDERR_STRUCTSIZE            0x0001
#define CDERR_INITIALIZATION        0x0002
#define CDERR_NOTEMPLATE            0x0003
#define CDERR_NOHINSTANCE           0x0004
#define CDERR_LOADSTRFAILURE        0x0005
#define CDERR_FINDRESFAILURE        0x0006
#define CDERR_LOADRESFAILURE        0x0007
#define CDERR_LOCKRESFAILURE        0x0008
#define CDERR_MEMALLOCFAILURE       0x0009
#define CDERR_MEMLOCKFAILURE        0x000A
#define CDERR_NOHOOK                0x000B
#define CDERR_REGISTERMSGFAIL       0x000C

// Window-class strings — used in CreateWindowEx calls.  Real Win32
// matches against registered class atoms; ours never register
// anything, so just expose the canonical name strings.
#define WC_TREEVIEWW           L"SysTreeView32"
#define WC_LISTVIEWW           L"SysListView32"
#define WC_HEADERW             L"SysHeader32"
#define WC_TABCONTROLW         L"SysTabControl32"
#define WC_STATUSBARW          L"msctls_statusbar32"
#define WC_TREEVIEWA            "SysTreeView32"
#define WC_LISTVIEWA            "SysListView32"
#if defined(UNICODE) || defined(_UNICODE)
#  define WC_TREEVIEW          WC_TREEVIEWW
#  define WC_LISTVIEW          WC_LISTVIEWW
#  define WC_HEADER            WC_HEADERW
#  define WC_TABCONTROL        WC_TABCONTROLW
#  define WC_STATUSBAR         WC_STATUSBARW
#else
#  define WC_TREEVIEW          WC_TREEVIEWA
#  define WC_LISTVIEW          WC_LISTVIEWA
#endif

// _wsplitpath — MSVC's path decomposer.  Backing by manual parse.
void _wsplitpath(const wchar_t *path, wchar_t *drive, wchar_t *dir,
                  wchar_t *fname, wchar_t *ext);
void _splitpath (const char    *path, char    *drive, char    *dir,
                  char    *fname, char    *ext);
void _wmakepath(wchar_t *path, const wchar_t *drive, const wchar_t *dir,
                 const wchar_t *fname, const wchar_t *ext);
typedef int errno_t;
errno_t _wsplitpath_s(const wchar_t *path,
                        wchar_t *drive, size_t dsz,
                        wchar_t *dir,   size_t dirsz,
                        wchar_t *fname, size_t fnsz,
                        wchar_t *ext,   size_t extsz);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // _WINUSER_H
