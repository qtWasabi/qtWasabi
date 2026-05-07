// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// wasabi-port-stubs.cpp — Linux/Qt implementations of the BFC platform
// hooks that the opensourced Wasabi source declares but provides only on
// Win32 / macOS.
//
// What's in here:
//   - MALLOC / FREE / REALLOC / CALLOC / WMALLOC / MEMCPY / MEMCPY32 /
//     MEMDUP / MEMFILL32 — Wasabi's malloc-family wrappers, here just
//     forwarders to the C library.  Wasabi's Win32 build adds optional
//     debug tracking on top; we don't.
//   - WCSICMP / WCSNICMP — case-insensitive wide-char compare,
//     forwarded to POSIX wcscasecmp / wcsncasecmp.
//   - _assert_handler / _assert_handler_str — BFC's ASSERT() targets.
//     Linux build prints to stderr and abort()s, matching the macOS
//     convention.
//
// What's NOT in here:
//   - timing / mouse / X11 hooks from wasabi_std.cpp — the Maki VM
//     never calls them (verified by grep on vcpu/scriptmgr/objecttable),
//     so they don't need to link.

#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <stdio.h>
#include <stdint.h>

// NOTE: BFC declares these in C++ linkage (no `extern "C"`), so the
// definitions here must match.  The compiler mangles them; the linker
// finds them by their C++ name.

// ── memory ───────────────────────────────────────────────────────
void  *MALLOC(size_t size)                            { return ::malloc(size); }
void   FREE(void *ptr)                                { ::free(ptr); }
void  *REALLOC(void *ptr, size_t size)                { return ::realloc(ptr, size); }
void  *CALLOC(size_t n, size_t sz)                    { return ::calloc(n, sz); }

wchar_t *WMALLOC(size_t size_in_bytes) {
    return static_cast<wchar_t *>(::malloc(size_in_bytes));
}

void   MEMCPY(void *dest, const void *src, size_t n)   { ::memcpy(dest, src, n); }
void   MEMCPY_(void *dest, const void *src, size_t n)  { ::memcpy(dest, src, n); }
void   MEMCPY32(void *dest, const void *src, size_t words) {
    ::memcpy(dest, src, words * sizeof(uint32_t));
}
void  *MEMDUP(const void *src, size_t n) {
    void *p = ::malloc(n);
    if (p && src) ::memcpy(p, src, n);
    return p;
}
void   MEMFILL32(void *ptr, unsigned long val, unsigned int n) {
    auto *p = static_cast<uint32_t *>(ptr);
    for (unsigned int i = 0; i < n; ++i) p[i] = static_cast<uint32_t>(val);
}

// ── wide-char compares ───────────────────────────────────────────
int WCSICMP(const wchar_t *a, const wchar_t *b) {
    while (*a && *b) {
        wint_t la = ::towlower(static_cast<wint_t>(*a));
        wint_t lb = ::towlower(static_cast<wint_t>(*b));
        if (la != lb) return static_cast<int>(la) - static_cast<int>(lb);
        ++a; ++b;
    }
    return static_cast<int>(::towlower(*a)) - static_cast<int>(::towlower(*b));
}

int WCSNICMP(const wchar_t *a, const wchar_t *b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        wint_t la = ::towlower(static_cast<wint_t>(a[i]));
        wint_t lb = ::towlower(static_cast<wint_t>(b[i]));
        if (la != lb) return static_cast<int>(la) - static_cast<int>(lb);
        if (!a[i])    return 0;
    }
    return 0;
}

// ── assertion handlers ───────────────────────────────────────────
void _assert_handler(const char *reason, const char *file, int line) {
    ::fprintf(stderr, "WasabiQT ASSERT: %s   at %s:%d\n",
              reason ? reason : "(null)", file, line);
    ::abort();
}

void _assert_handler_str(const char *str, const char *reason,
                         const char *file, int line) {
    ::fprintf(stderr, "WasabiQT ASSERT: %s — %s   at %s:%d\n",
              str    ? str    : "(null)",
              reason ? reason : "(null)", file, line);
    ::abort();
}

// MEMFILL<unsigned short> — primary template lives in std_mem.h, this
// specialisation is forward-declared there but has no in-tree body on
// non-Win32 builds.  Implement it here.
template<class T>
void MEMFILL(T *ptr, T val, unsigned int n);   // re-declare for ADL

template<>
void MEMFILL<unsigned short>(unsigned short *ptr, unsigned short val, unsigned int n) {
    for (unsigned int i = 0; i < n; ++i) ptr[i] = val;
}
