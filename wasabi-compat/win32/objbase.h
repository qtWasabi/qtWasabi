// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#ifndef _OBJBASE_H_INCLUDED_
#define _OBJBASE_H_INCLUDED_
//
// objbase.h — minimum-viable Win32 COM surface used by ml_*
// plugins that ship COM interfaces (IDispatch subclasses for
// scriptability, IDataObject for drag-drop, etc.).
//
// We don't ship a real COM apartment / aggregation / IPC.  Code
// paths that ACTUALLY invoke COM-runtime services (CoCreateInstance,
// QueryInterface across apartments, aggregated objects) will fail
// at link time, which is the correct outcome — they require a
// COM substrate we don't have.  Plain virtual-vtable usage
// (subclassing IDispatch and implementing GetIDsOfNames in C++)
// compiles fine because it's just C++ inheritance.
//

#include "basetsd.h"
#include "windef.h"
#include "wtypes.h"

#include <cstring>   // memcmp for GUID compare in BFC code
#include <cstdio>    // FILE / fopen for _wfopen shim

#ifdef __cplusplus
extern "C" {
#endif

// REFIID / IID / CLSID typedef'd in wtypes.h; this header just
// declares the wrappers + interfaces.  Forward-decl REFGUID_PTR
// for callers that prefer the pointer form.
typedef const GUID *REFGUID_PTR;

// IID_PPV_ARGS_Helper — Windows macro that pre-7 SDK shipped as a
// pair-of-args constructor; plugin code uses it inline.
#ifdef __cplusplus
#  define IID_PPV_ARGS(ppType) __uuidof(**(ppType)), \
                                reinterpret_cast<void **>(ppType)
#endif

// __uuidof — MSVC keyword.  Replace with no-op returning a null
// GUID; plugin code that actually consults the result still
// compiles, just returns 0-GUID.
#ifndef __uuidof
inline const GUID &qtwasabi_compat_null_uuid() {
    static const GUID g = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};
    return g;
}
#  define __uuidof(x) qtwasabi_compat_null_uuid()
#endif

// ── STDMETHOD macros ────────────────────────────────────────────
#ifndef STDMETHODCALLTYPE
#  define STDMETHODCALLTYPE
#endif
#define STDMETHOD(method)        virtual HRESULT STDMETHODCALLTYPE method
#define STDMETHOD_(type, method) virtual type    STDMETHODCALLTYPE method
#define STDMETHODIMP             HRESULT STDMETHODCALLTYPE
#define STDMETHODIMP_(type)      type    STDMETHODCALLTYPE
#define PURE                     = 0
#define DECLARE_INTERFACE(name)  struct name
#define DECLARE_INTERFACE_(name, parent) struct name : public parent
#define BEGIN_INTERFACE
#define END_INTERFACE

// MIDL_INTERFACE — MIDL-generated tagging.  No-op on our build;
// the inheriting interface is a regular C++ struct.
#define MIDL_INTERFACE(x) struct

// ── IUnknown — the base of every COM interface ─────────────────
#ifdef __cplusplus
}  // extern "C"

// Forward-decls for IDispatch's vtable argument types.  Declared
// up here so the IDispatch::Invoke signature below names them.
// VARIANT carries the real `vt` tag + payload union (used by
// VariantInit + V_VT macros further down).  DISPPARAMS / EXCEPINFO
// stay opaque — touching their members fails at compile, correct
// outcome for paths needing real COM runtime.
struct DISPPARAMS;  // defined below (after VARIANT)
struct EXCEPINFO  { int dummy; };
struct ITypeInfo;

struct VARIANTtag {
    WORD vt;
    WORD reserved1;
    WORD reserved2;
    WORD reserved3;
    union {
        long       lVal;
        long       ulVal;
        double     dblVal;
        float      fltVal;
        short      iVal;
        short      boolVal;
        IDispatch *pdispVal;
        IUnknown  *punkVal;
        wchar_t   *bstrVal;
        long      *plVal;
        void      *byref;
    };
};
#ifndef VARIANT_DEFINED
#define VARIANT_DEFINED
typedef struct VARIANTtag VARIANT;
typedef VARIANT          *LPVARIANT;
typedef VARIANT           VARIANTARG;
typedef VARIANT          *LPVARIANTARG;
#endif

// DISPPARAMS — IDispatch's Invoke parameter pack.  ml_playlists's
// COM class reads cArgs / rgvarg / cNamedArgs / rgdispidNamedArgs
// directly, so we ship the canonical Win32 layout.
struct DISPPARAMS {
    VARIANT *rgvarg;
    DISPID  *rgdispidNamedArgs;
    UINT     cArgs;
    UINT     cNamedArgs;
};

struct IUnknown {
    STDMETHOD(QueryInterface)(REFIID riid, void **ppv) PURE;
    STDMETHOD_(ULONG, AddRef)() PURE;
    STDMETHOD_(ULONG, Release)() PURE;
};

// ── IDispatch — scriptable COM dispatch interface ──────────────
struct IDispatch : public IUnknown {
    STDMETHOD(GetTypeInfoCount)(UINT *pctinfo) PURE;
    STDMETHOD(GetTypeInfo)(UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo) PURE;
    STDMETHOD(GetIDsOfNames)(REFIID riid, LPOLESTR *rgszNames,
                              UINT cNames, LCID lcid, DISPID *rgDispId) PURE;
    STDMETHOD(Invoke)(DISPID dispIdMember, REFIID riid, LCID lcid,
                       WORD wFlags, DISPPARAMS *pDispParams,
                       VARIANT *pVarResult, EXCEPINFO *pExcepInfo,
                       UINT *puArgErr) PURE;
};

extern "C" {
#endif  // __cplusplus

// LPOLESTR / DISPID typedef'd in wtypes.h.  DISPPARAMS / EXCEPINFO
// stay opaque; VARIANT has its full layout further down because
// VariantInit + V_VT macros need actual member access.
struct DISPPARAMS;
struct EXCEPINFO;

// CoInitialize / CoUninitialize — no-ops; we don't have a COM
// apartment.  CoCreateInstance always fails (returns E_NOTIMPL).
HRESULT WINAPI CoInitialize(LPVOID pvReserved);
HRESULT WINAPI CoInitializeEx(LPVOID pvReserved, DWORD dwCoInit);
void    WINAPI CoUninitialize(void);
HRESULT WINAPI CoCreateInstance(REFGUID rclsid, LPUNKNOWN pUnkOuter,
                                  DWORD dwClsContext, REFGUID riid,
                                  LPVOID *ppv);

// ── Canonical COM IIDs ─────────────────────────────────────────
// Win32 SDK declares these as `extern const IID IID_IUnknown` and
// friends.  We provide value-initialised storage in objbase
// implementations elsewhere (no header-only data).  These declare
// the names so callers can `&IID_IDispatch` for IsEqualIID compare.
extern const IID IID_IUnknown;
extern const IID IID_IDispatch;
extern const IID IID_IDataObject;

// IsEqualGUID / IsEqualIID — Win32 COM compare.  Defined inline
// because they're tiny and the call sites are pervasive.
#ifdef __cplusplus
inline BOOL IsEqualGUID(REFGUID a, REFGUID b) {
    return memcmp(&a, &b, sizeof(GUID)) == 0;
}
#define IsEqualIID(a, b)   IsEqualGUID((a), (b))
#define IsEqualCLSID(a, b) IsEqualGUID((a), (b))
#endif

// ── Dispatch constants ─────────────────────────────────────────
#define DISPID_UNKNOWN        ((DISPID)(-1))
#define DISPID_VALUE          ((DISPID)0)
#define DISPID_PROPERTYPUT    ((DISPID)(-3))
#define DISPID_NEWENUM        ((DISPID)(-4))
#define DISPID_EVALUATE       ((DISPID)(-5))
#define DISPID_CONSTRUCTOR    ((DISPID)(-6))
#define DISPID_DESTRUCTOR     ((DISPID)(-7))
#define DISPID_COLLECT        ((DISPID)(-8))

#define DISP_E_UNKNOWNINTERFACE  ((HRESULT)0x80020001L)
#define DISP_E_MEMBERNOTFOUND    ((HRESULT)0x80020003L)
#define DISP_E_PARAMNOTFOUND     ((HRESULT)0x80020004L)
#define DISP_E_TYPEMISMATCH      ((HRESULT)0x80020005L)
#define DISP_E_UNKNOWNNAME       ((HRESULT)0x80020006L)
#define DISP_E_NONAMEDARGS       ((HRESULT)0x80020007L)
#define DISP_E_BADVARTYPE        ((HRESULT)0x80020008L)
#define DISP_E_EXCEPTION         ((HRESULT)0x80020009L)
#define DISP_E_OVERFLOW          ((HRESULT)0x8002000AL)
#define DISP_E_BADINDEX          ((HRESULT)0x8002000BL)
#define DISP_E_UNKNOWNLCID       ((HRESULT)0x8002000CL)
#define DISP_E_BUFFERTOOSMALL    ((HRESULT)0x80020013L)
#define DISP_E_BADPARAMCOUNT     ((HRESULT)0x8002000EL)
#define DISP_E_PARAMNOTOPTIONAL  ((HRESULT)0x8002000FL)
#define DISP_E_BADCALLEE         ((HRESULT)0x80020010L)
#define DISP_E_NOTACOLLECTION    ((HRESULT)0x80020011L)

// VARIANT manipulation.  Real VARIANT has dozens of members;
// our forward-decl-as-empty-struct is enough for compile.
//
// VariantInit zeros out the struct.  V_VT extracts the type tag.
// The real VARIANT layout begins with a WORD vt followed by 3
// reserved WORDs; the macros below mirror Win32 SDK access
// patterns enough that plugin code compiles.

#define VT_EMPTY         0
#define VT_NULL          1
#define VT_I2            2
#define VT_I4            3
#define VT_R4            4
#define VT_R8            5
#define VT_BSTR          8
#define VT_DISPATCH      9
#define VT_ERROR        10
#define VT_BOOL         11
#define VT_VARIANT      12
#define VT_UNKNOWN      13
#define VT_I1           16
#define VT_UI1          17
#define VT_UI2          18
#define VT_UI4          19
#define VT_I8           20
#define VT_UI8          21
#define VT_INT          22
#define VT_UINT         23
#define VT_VOID         24
#define VT_PTR          26
#define VT_BYREF       0x4000

#define V_VT(v)         (((v))->vt)
#define V_I4(v)         (((v))->lVal)
#define V_BOOL(v)       (((v))->lVal)
#define V_DISPATCH(v)   (((v))->pdispVal)
#define V_UNKNOWN(v)    (((v))->punkVal)
#define V_R8(v)         (((v))->dblVal)
#define VARIANT_TRUE    (-1)
#define VARIANT_FALSE   (0)

// VARIANT — real layout would be much bigger.  Plugin code that
// uses it tag-only compiles; touching its data members fails at
// link/compile if the corresponding library isn't linked.
inline void VariantInit(VARIANT *v) {
    if (v) { v->vt = VT_EMPTY; v->reserved1 = v->reserved2 = v->reserved3 = 0; v->lVal = 0; }
}
inline HRESULT VariantClear(VARIANT *v) {
    if (v) v->vt = VT_EMPTY;
    return S_OK;
}

// _wcsicmp — wide case-insensitive compare.  Aliased to POSIX
// wcscasecmp from <wchar.h>.
#define _wcsicmp   wcscasecmp
#define _stricmp   strcasecmp
#define _wcsnicmp  wcsncasecmp
#define _strnicmp  strncasecmp
#define _wcsdup    wcsdup
#define _strdup    strdup
// _wfopen — MSVC's wide-char fopen.  POSIX has no native; we
// narrow-convert and call fopen.
#ifdef __cplusplus
inline FILE *_wfopen(const wchar_t *path, const wchar_t *mode) {
    if (!path || !mode) return nullptr;
    char npath[4096] = {0}, nmode[16] = {0};
    for (size_t i = 0; i < sizeof(npath) - 1 && path[i]; ++i)
        npath[i] = (char)(path[i] & 0xff);
    for (size_t i = 0; i < sizeof(nmode) - 1 && mode[i]; ++i)
        nmode[i] = (char)(mode[i] & 0xff);
    return std::fopen(npath, nmode);
}
#endif

// CharNextW — Win32 wide-string iterator (returns ptr + 1 for
// non-surrogate UTF-16, which Linux UCS-4 wchar_t doesn't have, so
// we always advance by one).
#ifdef __cplusplus
inline LPWSTR CharNextW(LPCWSTR p) {
    return p && *p ? const_cast<LPWSTR>(p + 1)
                   : const_cast<LPWSTR>(p);
}
inline LPSTR CharNextA(LPSTR p) {
    return p && *p ? (p + 1) : p;
}

// MSVC-only locale-aware string→number functions used in
// wasabi_std.h.  POSIX equivalent is wcstol/wcstod with
// locale set; we ignore the locale arg (the caller's `lcid`
// just selects the LANG, and our process locale defaults to
// the same one in practice).
// _locale_t is typedef'd to `int` in compat-shim.h.  Pre-declare
// the helpers so callers find them; bodies treat the value as
// opaque.
#ifndef _LOCALE_T_DEFINED
#define _LOCALE_T_DEFINED
typedef int _locale_t;
#endif
inline _locale_t _create_locale(int, const char *) { return 0; }
inline _locale_t _wcreate_locale(int, const wchar_t *) { return 0; }
inline void      _free_locale(_locale_t) {}
inline int       _wtoi(const wchar_t *s) {
    return s ? (int)wcstol(s, nullptr, 10) : 0;
}
inline int       _wtoi_l(const wchar_t *s, _locale_t) { return _wtoi(s); }
inline double    _wtof_l(const wchar_t *s, _locale_t) {
    return s ? wcstod(s, nullptr) : 0.0;
}
inline long      _strtol_l(const char *s, char **end, int base, _locale_t) {
    return s ? strtol(s, end, base) : 0;
}
inline long      _wtol_l (const wchar_t *s, _locale_t) {
    return s ? wcstol(s, nullptr, 10) : 0;
}
#endif

// ── BSTR / SAFEARRAY (OLE Automation) ─────────────────────────
// We don't ship a real BSTR — just typedef it to wchar_t* so
// VARIANT.bstrVal compiles.  Callers that actually CALL
// SysAllocString or read the prefix length will hit our null
// helpers, which is the documented "not supported" path.
typedef wchar_t *BSTR;

// SAFEARRAY — opaque; ml_playlists only uses pointers.
struct SAFEARRAY { void *_opaque; };

#ifdef __cplusplus
inline SAFEARRAY *SafeArrayCreateVector(WORD /*vt*/, LONG /*lo*/, ULONG /*n*/) {
    return nullptr;
}
inline HRESULT SafeArrayAccessData  (SAFEARRAY *, void **p) {
    if (p) *p = nullptr;
    return (HRESULT)0x80004005L;  // E_FAIL
}
inline HRESULT SafeArrayUnaccessData(SAFEARRAY *) { return 0; }
inline HRESULT SafeArrayDestroy     (SAFEARRAY *) { return 0; }
#endif

#define VT_ARRAY    0x2000
#define V_ARRAY(v)  ((v)->byref)

// LOCALE_USER_DEFAULT — Win32 locale constant used by
// CompareStringW.  We treat any LCID identically (POSIX has no
// per-call locale switching).
#ifndef LOCALE_USER_DEFAULT
#  define LOCALE_USER_DEFAULT     0x0400
#endif
#ifndef LOCALE_SYSTEM_DEFAULT
#  define LOCALE_SYSTEM_DEFAULT   0x0800
#endif
#ifndef LOCALE_INVARIANT
#  define LOCALE_INVARIANT        0x007F
#endif

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // _OBJBASE_H_INCLUDED_
