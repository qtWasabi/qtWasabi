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
#include <ctype.h>
#include <strings.h>

// NOTE: BFC declares these in C++ linkage (no `extern "C"`), so the
// definitions here must match.  The compiler mangles them; the linker
// finds them by their C++ name.

// ── memory ───────────────────────────────────────────────────────
void  *MALLOC(size_t size)                            { return ::malloc(size); }
void   FREE(void *ptr)                                { ::free(ptr); }
void  *REALLOC(void *ptr, size_t size)                { return ::realloc(ptr, size); }
void  *CALLOC(size_t n, size_t sz)                    { return ::calloc(n, sz); }

// Upstream's WMALLOC takes a wchar_t **count** (and multiplies by
// sizeof(wchar_t) internally), it does NOT take a byte count.  Match
// that semantics: every existing caller in vcpu.cpp / StringW.cpp /
// wasabi_std.cpp passes (length + 1) where length is a number of
// wchars.  Treating the argument as a byte count under-allocates by
// 3x on Linux (sizeof(wchar_t) == 4) and lets the very next write
// scribble into the next heap chunk's metadata.  Caused the
// "corrupted size vs. prev_size" abort during sustained dispatch.
wchar_t *WMALLOC(size_t count) {
    return static_cast<wchar_t *>(::malloc(count * sizeof(wchar_t)));
}

// Upstream wasabi_std.cpp documents MEMCPY as "allows dest and src to
// overlap" and PtrList::removeByPos relies on that overlap-safe shift.
// Use memmove so we honour that contract.  The plain libc memcpy is
// undefined for overlapping ranges and ASAN trips on it during
// PtrList compaction.
void   MEMCPY(void *dest, const void *src, size_t n)   { ::memmove(dest, src, n); }
void   MEMCPY_(void *dest, const void *src, size_t n)  { ::memmove(dest, src, n); }
void   MEMCPY32(void *dest, const void *src, size_t words) {
    ::memmove(dest, src, words * sizeof(uint32_t));
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
// Set WASABIQT_FATAL_ASSERTS=1 to abort on first ASSERT (default
// during dev would crash the test runner).  By default we log and
// continue — script bytecode hits these on the first stub-returning-
// default we haven't filled in yet, and we want to see how far things
// got rather than dying immediately.
static bool fatal_asserts() {
    const char *e = ::getenv("WASABIQT_FATAL_ASSERTS");
    return e && *e == '1';
}

// Forward-declared instead of #include "maki-bridge.h" so this TU
// stays free of bridge-side dependencies in case anything pulls it in
// before the bridge header is reachable.
namespace WasabiQt::Maki { void getVmState(int *vsd, int *vip, int *vsp); }

static void print_vm_context(::FILE *out) {
    int vsd = -1, vip = -1, vsp = -1;
    WasabiQt::Maki::getVmState(&vsd, &vip, &vsp);
    ::fprintf(out, "  vm: sid=%d ip=%d vsp=%d\n", vsd, vip, vsp);
}

void _assert_handler(const char *reason, const char *file, int line) {
    ::fprintf(stderr, "[wasabiqt-assert] %s   at %s:%d\n",
              reason ? reason : "(null)", file, line);
    print_vm_context(stderr);
    if (fatal_asserts()) ::abort();
}

void _assert_handler_str(const char *str, const char *reason,
                         const char *file, int line) {
    ::fprintf(stderr, "[wasabiqt-assert] %s — %s   at %s:%d\n",
              str    ? str    : "(null)",
              reason ? reason : "(null)", file, line);
    print_vm_context(stderr);
    if (fatal_asserts()) ::abort();
}

// ── wasabi_std.cpp helpers used by string/StringW.cpp ────────────
// Wasabi's bfc/wasabi_std.h declares these as wrappers around C string
// functions (with optional debug instrumentation on Win32).  We're
// not vendoring wasabi_std.cpp, so provide forwarders.

int    STRLEN(const char *s)                                   { return s ? (int)::strlen(s) : 0; }
char  *STRCPY(char *d, const char *s)                          { return ::strcpy(d, s); }
char  *STRNCPY(char *d, const char *s, int n)                  { ::strncpy(d, s, n); if (n > 0) d[n-1] = 0; return d; }
int    STRCMP(const char *a, const char *b)                    { return ::strcmp(a, b); }
const char *STRSTR(const char *h, const char *n)               { return ::strstr(h, n); }
char  *STRTOLOWER(char *s)                                     { for (char *p = s; p && *p; ++p) *p = (char)::tolower((unsigned char)*p); return s; }
char  *STRTOUPPER(char *s)                                     { for (char *p = s; p && *p; ++p) *p = (char)::toupper((unsigned char)*p); return s; }
int    STRCMPSAFE(const char *a, const char *b, const char *da, const char *db)
                                                               { return ::strcmp(a ? a : (da ? da : ""), b ? b : (db ? db : "")); }
int    STRICMPSAFE(const char *a, const char *b, const char *da, const char *db)
                                                               { return ::strcasecmp(a ? a : (da ? da : ""), b ? b : (db ? db : "")); }

int    ISDIGIT(wchar_t c)                                      { return (c >= L'0' && c <= L'9') ? 1 : 0; }
wchar_t *WCSCPYN(wchar_t *d, const wchar_t *s, unsigned long n) { ::wcsncpy(d, s, n); if (n > 0) d[n-1] = 0; return d; }
int    WCSICMPSAFE(const wchar_t *a, const wchar_t *b, const wchar_t *da, const wchar_t *db)
                                                               { return WCSICMP(a ? a : (da ? da : L""), b ? b : (db ? db : L"")); }

// Directory separator character — '/' on POSIX, '\\' on Win32.
namespace Wasabi { namespace Std {
    char dirChar() { return '/'; }
}}

// MEMFILL<unsigned short> — primary template lives in std_mem.h, this
// specialisation is forward-declared there but has no in-tree body on
// non-Win32 builds.  Implement it here.
template<class T>
void MEMFILL(T *ptr, T val, unsigned int n);   // re-declare for ADL

template<>
void MEMFILL<unsigned short>(unsigned short *ptr, unsigned short val, unsigned int n) {
    for (unsigned int i = 0; i < n; ++i) ptr[i] = val;
}
