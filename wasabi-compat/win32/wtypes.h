// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#ifndef _WTYPES_H
#define _WTYPES_H
//
// wtypes.h — minimum subset of Win32's COM types header that
// ml_* plugins reference at the top of their TUs.  Most just need
// the fundamental types from windef.h to be in scope; we forward
// to the broader umbrella so a `#include <wtypes.h>` brings in
// everything our windef.h already publishes.
//

#include "windef.h"
#include "winuser.h"

#ifdef __cplusplus
extern "C" {
#endif

// LCID / LANGID for code paths that touch CSTR_INVARIANT.
typedef DWORD LCID;
typedef WORD  LANGID;

#ifndef LANG_ENGLISH
#  define LANG_ENGLISH         0x09
#endif
#ifndef SUBLANG_ENGLISH_US
#  define SUBLANG_ENGLISH_US   0x01
#endif
#ifndef SORT_DEFAULT
#  define SORT_DEFAULT         0x0
#endif

#ifndef MAKELANGID
#  define MAKELANGID(p, s)     ((((WORD)(s)) << 10) | (WORD)(p))
#endif
#ifndef MAKELCID
#  define MAKELCID(lid, srtid) ((DWORD)((((DWORD)((WORD)(srtid))) << 16) | \
                                          ((DWORD)((WORD)(lid)))))
#endif

// GUID — fundamental COM identifier.  Win32 SDK byte order so
// cross-process payload comparisons stay wire-compatible.  Guard
// is double-named to suppress redefinition from upstream code
// that pre-defines either macro.
#if !defined(_GUID_DEFINED) && !defined(GUID_DEFINED)
#define GUID_DEFINED
#define _GUID_DEFINED
typedef struct _GUID {
    DWORD Data1;
    WORD  Data2;
    WORD  Data3;
    BYTE  Data4[8];
} GUID;
#endif

// REFIID / REFGUID / IID — typedef'd here so objbase.h's vtable
// declarations can name them.  C++ prefers reference form; C
// callers cast through pointer.
#ifdef __cplusplus
typedef const GUID &REFGUID;
typedef const GUID &REFIID;
typedef const GUID &REFCLSID;
#else
typedef const GUID *REFGUID;
typedef const GUID *REFIID;
typedef const GUID *REFCLSID;
#endif
typedef GUID IID;
typedef GUID CLSID;

// OLECHAR / LPOLESTR — OLE-style wide strings (always wchar_t).
typedef wchar_t        OLECHAR;
typedef wchar_t       *LPOLESTR;
typedef const wchar_t *LPCOLESTR;
typedef LONG           DISPID;
typedef LONG           SCODE;

// COM interface forward-decls — fully fleshed out in objbase.h
// (which plugin code includes when it actually subclasses these).
// Keep just the forward decls here so wtypes-only consumers can
// declare LPUNKNOWN/LPDISPATCH typedefs.
struct IUnknown;
struct IDispatch;
struct IDataObject;
typedef struct IUnknown    *LPUNKNOWN;
typedef struct IDispatch   *LPDISPATCH;
typedef struct IDataObject *LPDATAOBJECT;

// HRESULT canonical error codes.  Numbers match Win32 SDK so
// payloads remain wire-compatible with upstream comparisons.
#define S_OK              ((HRESULT)0L)
#define S_FALSE           ((HRESULT)1L)
#define E_NOTIMPL         ((HRESULT)0x80004001L)
#define E_NOINTERFACE     ((HRESULT)0x80004002L)
#define E_POINTER         ((HRESULT)0x80004003L)
#define E_ABORT           ((HRESULT)0x80004004L)
#define E_FAIL            ((HRESULT)0x80004005L)
#define E_OUTOFMEMORY     ((HRESULT)0x8007000EL)
#define E_INVALIDARG      ((HRESULT)0x80070057L)
#define E_HANDLE          ((HRESULT)0x80070006L)
#define E_ACCESSDENIED    ((HRESULT)0x80070005L)
#define E_UNEXPECTED      ((HRESULT)0x8000FFFFL)

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // _WTYPES_H
