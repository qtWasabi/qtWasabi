// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#ifndef _STRSAFE_H_INCLUDED_
#define _STRSAFE_H_INCLUDED_
//
// strsafe.h — Win32's "safe" string format family.  ml_nowplaying
// + most ml_* plugins use these instead of raw `sprintf`/`wcscpy`
// to get explicit length-bounded ops.
//
// All routines return HRESULT.  Success = S_OK (0).  Failure
// returns STRSAFE_E_INSUFFICIENT_BUFFER (0x8007007A) when the
// supplied buffer was too small; we map both cleanly to <cwchar>
// + <cstring> backings.
//

#include "basetsd.h"
#include "windef.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>

#define S_OK                            0L
#define STRSAFE_E_INSUFFICIENT_BUFFER   ((HRESULT)0x8007007A)
#define STRSAFE_E_INVALID_PARAMETER     ((HRESULT)0x80070057)
#define STRSAFE_E_END_OF_FILE           ((HRESULT)0x80070026)

// Cast through `long` so callers passing pointer-shaped HRESULT
// values (some plugin code intermixes types) don't trip the
// "ordered comparison of pointer with integer" warning under
// `-fpermissive`.
#define SUCCEEDED(hr)  (((long)(hr)) >= 0)
#define FAILED(hr)     (((long)(hr)) <  0)

#ifdef __cplusplus
extern "C" {
#endif

// Compute the wide-string length up to `cchMax`, never reading
// past.  Returns the count via *pcch.
static inline HRESULT StringCchLengthW(const wchar_t *psz,
                                         size_t cchMax,
                                         size_t *pcch) {
    if (!psz || !pcch) return STRSAFE_E_INVALID_PARAMETER;
    size_t i = 0;
    while (i < cchMax && psz[i] != 0) ++i;
    if (i == cchMax) return STRSAFE_E_INVALID_PARAMETER;
    *pcch = i;
    return S_OK;
}

static inline HRESULT StringCchLengthA(const char *psz,
                                         size_t cchMax,
                                         size_t *pcch) {
    if (!psz || !pcch) return STRSAFE_E_INVALID_PARAMETER;
    size_t i = 0;
    while (i < cchMax && psz[i] != 0) ++i;
    if (i == cchMax) return STRSAFE_E_INVALID_PARAMETER;
    *pcch = i;
    return S_OK;
}

static inline HRESULT StringCchCopyW(wchar_t *dst, size_t cchDst,
                                       const wchar_t *src) {
    if (!dst || cchDst == 0 || !src) return STRSAFE_E_INVALID_PARAMETER;
    size_t i = 0;
    for (; i + 1 < cchDst && src[i] != 0; ++i) dst[i] = src[i];
    dst[i] = 0;
    return src[i] == 0 ? S_OK : STRSAFE_E_INSUFFICIENT_BUFFER;
}

static inline HRESULT StringCchCopyA(char *dst, size_t cchDst,
                                       const char *src) {
    if (!dst || cchDst == 0 || !src) return STRSAFE_E_INVALID_PARAMETER;
    size_t i = 0;
    for (; i + 1 < cchDst && src[i] != 0; ++i) dst[i] = src[i];
    dst[i] = 0;
    return src[i] == 0 ? S_OK : STRSAFE_E_INSUFFICIENT_BUFFER;
}

static inline HRESULT StringCchCatW(wchar_t *dst, size_t cchDst,
                                      const wchar_t *src) {
    if (!dst || cchDst == 0 || !src) return STRSAFE_E_INVALID_PARAMETER;
    size_t pos = 0;
    while (pos < cchDst && dst[pos] != 0) ++pos;
    if (pos == cchDst) return STRSAFE_E_INVALID_PARAMETER;
    size_t i = 0;
    for (; pos + 1 < cchDst && src[i] != 0; ++pos, ++i) dst[pos] = src[i];
    dst[pos] = 0;
    return src[i] == 0 ? S_OK : STRSAFE_E_INSUFFICIENT_BUFFER;
}

static inline HRESULT StringCchVPrintfW(wchar_t *dst, size_t cchDst,
                                          const wchar_t *fmt,
                                          va_list args) {
    if (!dst || cchDst == 0 || !fmt) return STRSAFE_E_INVALID_PARAMETER;
    int n = std::vswprintf(dst, cchDst, fmt, args);
    if (n < 0) {
        dst[cchDst - 1] = 0;
        return STRSAFE_E_INSUFFICIENT_BUFFER;
    }
    return S_OK;
}

static inline HRESULT StringCchPrintfW(wchar_t *dst, size_t cchDst,
                                         const wchar_t *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    HRESULT hr = StringCchVPrintfW(dst, cchDst, fmt, args);
    va_end(args);
    return hr;
}

static inline HRESULT StringCchPrintfA(char *dst, size_t cchDst,
                                         const char *fmt, ...) {
    if (!dst || cchDst == 0 || !fmt) return STRSAFE_E_INVALID_PARAMETER;
    va_list args;
    va_start(args, fmt);
    int n = std::vsnprintf(dst, cchDst, fmt, args);
    va_end(args);
    if (n < 0 || static_cast<size_t>(n) >= cchDst) {
        dst[cchDst - 1] = 0;
        return STRSAFE_E_INSUFFICIENT_BUFFER;
    }
    return S_OK;
}

#ifndef ARRAYSIZE
#  define ARRAYSIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

// StringCchCopyEx — strsafe extended variant with flag tolerance.
// Real Win32 honours STRSAFE_IGNORE_NULLS / STRSAFE_FILL_BEHIND
// flags; ours just forwards to plain StringCchCopy when args are
// non-null.
#define STRSAFE_IGNORE_NULLS       0x00000001
#define STRSAFE_FILL_BEHIND_NULL   0x00000002
#define STRSAFE_FILL_ON_FAILURE    0x00000004
#define STRSAFE_NULL_ON_FAILURE    0x00000008
#define STRSAFE_NO_TRUNCATION      0x00000010
#define STRSAFE_IGNORE_NULL_UNICODE_STRINGS 0x00000020

static inline HRESULT StringCchCopyExW(wchar_t *dst, size_t cchDst,
                                         const wchar_t *src,
                                         wchar_t ** /*ppszDstEnd*/,
                                         size_t * /*pcchRemaining*/,
                                         DWORD flags) {
    if (!src && (flags & STRSAFE_IGNORE_NULLS)) {
        if (dst && cchDst > 0) dst[0] = 0;
        return S_OK;
    }
    return StringCchCopyW(dst, cchDst, src);
}
static inline HRESULT StringCchCopyExA(char *dst, size_t cchDst,
                                         const char *src,
                                         char ** /*ppszDstEnd*/,
                                         size_t * /*pcchRemaining*/,
                                         DWORD flags) {
    if (!src && (flags & STRSAFE_IGNORE_NULLS)) {
        if (dst && cchDst > 0) dst[0] = 0;
        return S_OK;
    }
    return StringCchCopyA(dst, cchDst, src);
}

#if defined(UNICODE) || defined(_UNICODE)
#  define StringCchCopyEx StringCchCopyExW
#else
#  define StringCchCopyEx StringCchCopyExA
#endif

// UNICODE-aliased symbols.  Win32 SDK provides these as either
// W or A based on UNICODE define; we follow the same convention.
#if defined(UNICODE) || defined(_UNICODE)
#  define StringCchPrintf  StringCchPrintfW
#  define StringCchCopy    StringCchCopyW
#  define StringCchCat     StringCchCatW
#  define StringCchLength  StringCchLengthW
#else
#  define StringCchPrintf  StringCchPrintfA
#  define StringCchCopy    StringCchCopyA
#  define StringCchCat     StringCchCatA
#  define StringCchLength  StringCchLengthA
#endif

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // _STRSAFE_H_INCLUDED_
