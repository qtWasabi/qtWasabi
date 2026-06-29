// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// mbstring.h — stub for MSVC's <mbstring.h> (multibyte-string CRT).
// Winamp/Main.h includes it; the in-player playlist render path
// (draw_pe.cpp) pulls Main.h.  qtamp runs Unicode (wchar_t) end-to-end,
// so the _mbs* multibyte helpers are unused — provide the few names that
// appear in headers, mapped onto the standard byte-string CRT, so the
// include resolves without dragging in a real MBCS layer.

#ifndef QTWASABI_MBSTRING_SHIM_H
#define QTWASABI_MBSTRING_SHIM_H

#include <cstring>
#include <cstdlib>

// _mbs* operate on `unsigned char *` in MSVC.  Map the handful that
// turn up onto the plain C string functions (single-byte locale).
static inline unsigned char *_mbsinc(const unsigned char *p) {
    return const_cast<unsigned char *>(p + 1);
}
static inline unsigned char *_mbsdec(const unsigned char *start,
                                     const unsigned char *cur) {
    return cur > start ? const_cast<unsigned char *>(cur - 1) : nullptr;
}
static inline size_t _mbslen(const unsigned char *s) {
    return std::strlen(reinterpret_cast<const char *>(s));
}
static inline unsigned char *_mbschr(const unsigned char *s, unsigned int c) {
    return reinterpret_cast<unsigned char *>(
        const_cast<char *>(std::strchr(reinterpret_cast<const char *>(s),
                                        static_cast<int>(c))));
}
static inline int _mbscmp(const unsigned char *a, const unsigned char *b) {
    return std::strcmp(reinterpret_cast<const char *>(a),
                       reinterpret_cast<const char *>(b));
}

#endif  // QTWASABI_MBSTRING_SHIM_H
