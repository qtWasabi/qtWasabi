// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once
//
// shlwapi.h — Win32 Shell Light-Weight Utility API.  ml_playlists
// relies on the Path* utilities + a few legacy string helpers; provide
// them inline so the source compiles unchanged.
//

#include <basetsd.h>
#include <windef.h>
#include <cstring>
#include <cwchar>
#include <cwctype>

#ifdef __cplusplus
extern "C" {
#endif

// ── Path helpers ───────────────────────────────────────────────
// PathFindFileNameW(L"foo/bar/baz.m3u") → L"baz.m3u"
static inline LPWSTR PathFindFileNameW(LPCWSTR path) {
    if (!path) return nullptr;
    const wchar_t *last = path;
    for (const wchar_t *p = path; *p; ++p) {
        if (*p == L'\\' || *p == L'/') last = p + 1;
    }
    return const_cast<LPWSTR>(last);
}
static inline LPSTR PathFindFileNameA(LPCSTR path) {
    if (!path) return nullptr;
    const char *last = path;
    for (const char *p = path; *p; ++p) {
        if (*p == '\\' || *p == '/') last = p + 1;
    }
    return const_cast<LPSTR>(last);
}

// PathFindExtensionW(L"foo.m3u") → L".m3u"
static inline LPWSTR PathFindExtensionW(LPCWSTR path) {
    if (!path) return nullptr;
    const wchar_t *dot = nullptr;
    for (const wchar_t *p = path; *p; ++p) {
        if (*p == L'.') dot = p;
        else if (*p == L'\\' || *p == L'/') dot = nullptr;
    }
    return dot ? const_cast<LPWSTR>(dot)
               : const_cast<LPWSTR>(path + wcslen(path));
}
static inline LPSTR PathFindExtensionA(LPCSTR path) {
    if (!path) return nullptr;
    const char *dot = nullptr;
    for (const char *p = path; *p; ++p) {
        if (*p == '.') dot = p;
        else if (*p == '\\' || *p == '/') dot = nullptr;
    }
    return dot ? const_cast<LPSTR>(dot)
               : const_cast<LPSTR>(path + std::strlen(path));
}

// PathFileExistsW — true if the path resolves to a regular file
// or directory on the local filesystem.
static inline BOOL PathFileExistsW(LPCWSTR path) {
    if (!path) return FALSE;
    char narrow[4096] = {0};
    size_t i = 0;
    for (; i < sizeof(narrow) - 1 && path[i]; ++i)
        narrow[i] = (char)(path[i] & 0xff);
    narrow[i] = 0;
    return std::fopen(narrow, "rb") ? TRUE : FALSE;  // crude but works
}
static inline BOOL PathFileExistsA(LPCSTR path) {
    return (path && std::fopen(path, "rb")) ? TRUE : FALSE;
}

// PathCombineW(out, dir, file) → out = dir + "\" + file
static inline LPWSTR PathCombineW(LPWSTR out, LPCWSTR dir, LPCWSTR file) {
    if (!out) return nullptr;
    out[0] = 0;
    size_t pos = 0;
    if (dir) {
        for (; dir[pos]; ++pos) out[pos] = dir[pos];
        if (pos > 0 && out[pos - 1] != L'\\' && out[pos - 1] != L'/') {
            out[pos++] = L'/';
        }
    }
    if (file) {
        size_t i = 0;
        // Skip leading separator on `file` if dir already ends in one.
        if (pos > 0 && (file[0] == L'\\' || file[0] == L'/'))
            i = 1;
        for (; file[i]; ++i, ++pos) out[pos] = file[i];
    }
    out[pos] = 0;
    return out;
}

// PathRemoveFileSpecW — strip last component (file name).  Returns
// TRUE if the path was modified.
static inline BOOL PathRemoveFileSpecW(LPWSTR path) {
    if (!path) return FALSE;
    size_t len = wcslen(path);
    if (len == 0) return FALSE;
    size_t i = len;
    while (i > 0 && path[i - 1] != L'\\' && path[i - 1] != L'/') --i;
    if (i == 0) return FALSE;
    path[i - 1] = 0;
    return TRUE;
}

// PathRemoveExtensionW — strip everything from the last `.` on.
static inline void PathRemoveExtensionW(LPWSTR path) {
    if (!path) return;
    LPWSTR dot = nullptr;
    for (LPWSTR p = path; *p; ++p) {
        if (*p == L'.') dot = p;
        else if (*p == L'\\' || *p == L'/') dot = nullptr;
    }
    if (dot) *dot = 0;
}
static inline void PathRemoveExtensionA(LPSTR path) {
    if (!path) return;
    LPSTR dot = nullptr;
    for (LPSTR p = path; *p; ++p) {
        if (*p == '.') dot = p;
        else if (*p == '\\' || *p == '/') dot = nullptr;
    }
    if (dot) *dot = 0;
}

// PathStripPathW — strip everything before the last separator.
static inline void PathStripPathW(LPWSTR path) {
    if (!path) return;
    LPWSTR last = nullptr;
    for (LPWSTR p = path; *p; ++p) {
        if (*p == L'\\' || *p == L'/') last = p;
    }
    if (last) {
        wchar_t *src = last + 1;
        wchar_t *dst = path;
        while ((*dst++ = *src++)) {}
    }
}

// PathIsURLW — true if the path starts with a scheme://
static inline BOOL PathIsURLW(LPCWSTR path) {
    if (!path || !*path) return FALSE;
    for (const wchar_t *p = path; *p; ++p) {
        if (*p == L':' && p[1] == L'/' && p[2] == L'/') return TRUE;
        if (*p == L'\\' || *p == L'/') return FALSE;
    }
    return FALSE;
}

// StrChrW — wide strchr.
static inline LPWSTR StrChrW(LPCWSTR s, wchar_t c) {
    if (!s) return nullptr;
    for (; *s; ++s) {
        if (*s == c) return const_cast<LPWSTR>(s);
    }
    return nullptr;
}
static inline LPWSTR StrRChrW(LPCWSTR s, LPCWSTR end, wchar_t c) {
    if (!s) return nullptr;
    LPCWSTR last = nullptr;
    while (s != end && *s) {
        if (*s == c) last = s;
        ++s;
    }
    return const_cast<LPWSTR>(last);
}

// PathIsFileSpecW — true iff no separator present.
static inline BOOL PathIsFileSpecW(LPCWSTR path) {
    if (!path) return FALSE;
    for (const wchar_t *p = path; *p; ++p) {
        if (*p == L'/' || *p == L'\\') return FALSE;
    }
    return TRUE;
}

// PathIsRelativeW — anything not starting with `/` or drive-letter.
static inline BOOL PathIsRelativeW(LPCWSTR path) {
    if (!path || !*path) return TRUE;
    if (path[0] == L'/' || path[0] == L'\\') return FALSE;
    if (path[1] == L':') return FALSE;
    return TRUE;
}

// ── String helpers ─────────────────────────────────────────────
// lstrcmpiW / lstrcmpiA — case-insensitive compare.  Real impl
// uses CompareString; we punt to wcscasecmp + strcasecmp.
static inline int lstrcmpiW(LPCWSTR a, LPCWSTR b) {
    if (!a) return b ? -1 : 0;
    if (!b) return 1;
    return wcscasecmp(a, b);
}
static inline int lstrcmpiA(LPCSTR a, LPCSTR b) {
    if (!a) return b ? -1 : 0;
    if (!b) return 1;
    return strcasecmp(a, b);
}
static inline int lstrcmpW(LPCWSTR a, LPCWSTR b) {
    if (!a) return b ? -1 : 0;
    if (!b) return 1;
    return wcscmp(a, b);
}
static inline int lstrcmpA(LPCSTR a, LPCSTR b) {
    if (!a) return b ? -1 : 0;
    if (!b) return 1;
    return std::strcmp(a, b);
}

// lstrcpyW — unsafe copy, but used heavily in legacy Win32 code.
// Our implementation has no buffer-length check; matches Win32.
static inline LPWSTR lstrcpyW(LPWSTR dst, LPCWSTR src) {
    if (!dst) return nullptr;
    LPWSTR ret = dst;
    while ((*dst++ = (src ? *src++ : 0)) != 0) {}
    return ret;
}
static inline LPSTR lstrcpyA(LPSTR dst, LPCSTR src) {
    if (!dst) return nullptr;
    LPSTR ret = dst;
    while ((*dst++ = (src ? *src++ : 0)) != 0) {}
    return ret;
}

// StrStrIW / StrStrIA — case-insensitive substring search.
static inline LPWSTR StrStrIW(LPCWSTR hay, LPCWSTR needle) {
    if (!hay || !needle || !*needle) return const_cast<LPWSTR>(hay);
    size_t nlen = wcslen(needle);
    for (const wchar_t *p = hay; *p; ++p) {
        if (wcsncasecmp(p, needle, nlen) == 0)
            return const_cast<LPWSTR>(p);
    }
    return nullptr;
}

// UNICODE-aliased macros — Win32 SDK convention.
#if defined(UNICODE) || defined(_UNICODE)
#  define PathFindExtension   PathFindExtensionW
#  define PathFindFileName    PathFindFileNameW
#  define PathFileExists      PathFileExistsW
#  define PathCombine         PathCombineW
#  define PathRemoveFileSpec  PathRemoveFileSpecW
#  define PathRemoveExtension PathRemoveExtensionW
#  define PathStripPath       PathStripPathW
#  define PathIsRelative      PathIsRelativeW
#  define lstrcmpi            lstrcmpiW
#  define lstrcmp             lstrcmpW
#  define lstrcpy             lstrcpyW
#  define StrStrI             StrStrIW
#else
#  define PathFindExtension   PathFindExtensionA
#  define PathFindFileName    PathFindFileNameA
#  define PathFileExists      PathFileExistsA
#  define PathRemoveExtension PathRemoveExtensionA
#  define lstrcmpi            lstrcmpiA
#  define lstrcmp             lstrcmpA
#  define lstrcpy             lstrcpyA
#endif

#ifdef __cplusplus
}  // extern "C"
#endif
