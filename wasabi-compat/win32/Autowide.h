// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#ifndef AUTOWIDEH
#define AUTOWIDEH
// Coordinate with upstream `nu/AutoChar.h` — its guard is
// `NULLSOFT_AUTOCHARH`.  Set both so upstream's body is skipped
// when included AFTER our shim.
#define NULLSOFT_AUTOCHARH
#define NULLSOFT_AUTOWIDEH
//
// Autowide.h — replicant/nu/AutoWide.h ports the upstream header
// minus the `#ifdef WIN32` gate.  The upstream version skips
// the class definition on non-Win32 builds, leaving plugin code
// that instantiates AutoWide(string) referencing an undefined
// type.  Our version provides the same class shape backed by
// our `MultiByteToWideChar` shim — no `#ifdef WIN32` needed.
//
// AutoWide(const char *) — converts UTF-8 (or the supplied
// codepage) to wchar_t, owns the buffer for the object's
// lifetime.  Implicit cast to `wchar_t *` lets plugin code
// pass the result wherever a wide string is wanted.
//

#include "win32/windef.h"

#include <cstdlib>
#include <cstring>

inline wchar_t *AutoWideDup(const char *convert, UINT codePage = 0) {
    if (!convert) return nullptr;
    int n = MultiByteToWideChar(codePage, 0, convert, -1, nullptr, 0);
    if (n <= 0) return nullptr;
    wchar_t *wide = static_cast<wchar_t *>(std::malloc(n * sizeof(wchar_t)));
    if (!wide) return nullptr;
    MultiByteToWideChar(codePage, 0, convert, -1, wide, n);
    return wide;
}

class AutoWide {
public:
    AutoWide(const char *src, UINT codePage = 0)
        : m_wide(AutoWideDup(src, codePage)) {}
    ~AutoWide() { if (m_wide) std::free(m_wide); }

    AutoWide(const AutoWide &)            = delete;
    AutoWide &operator=(const AutoWide &) = delete;

    operator wchar_t *()       { return m_wide; }
    operator const wchar_t *() const { return m_wide; }
    wchar_t *Get()             { return m_wide; }
    const wchar_t *Get() const { return m_wide; }

private:
    wchar_t *m_wide;
};

class AutoChar {
public:
    AutoChar(const wchar_t *src, UINT codePage = 0) : m_narrow(nullptr) {
        if (!src) return;
        int n = WideCharToMultiByte(codePage, 0, src, -1,
                                      nullptr, 0, nullptr, nullptr);
        if (n <= 0) return;
        m_narrow = static_cast<char *>(std::malloc(n));
        if (!m_narrow) return;
        WideCharToMultiByte(codePage, 0, src, -1, m_narrow, n,
                              nullptr, nullptr);
    }
    ~AutoChar() { if (m_narrow) std::free(m_narrow); }

    AutoChar(const AutoChar &)            = delete;
    AutoChar &operator=(const AutoChar &) = delete;

    operator char *()       { return m_narrow; }
    operator const char *() const { return m_narrow; }
    char *Get()             { return m_narrow; }
    const char *Get() const { return m_narrow; }

private:
    char *m_narrow;
};

#endif  // AUTOWIDEH
