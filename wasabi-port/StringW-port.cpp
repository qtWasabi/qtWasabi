// SPDX-License-Identifier: MIT
//
// StringW-port.cpp — compiled in place of upstream's StringW.cpp.
//
// Two upstream issues to paper over for Linux:
//
// 1. `StringW::va_sprintf` has only `#if defined(_WIN32)` and
//    `#elif defined(__APPLE__)` branches — no Linux fallthrough, so
//    on Linux the function returns garbage and SIGTRAPs through the
//    next caller.
//
// 2. Even with the macOS branch's `vswprintf`, Wasabi's wide-format
//    strings use `%s` with `wchar_t*` (Win32 convention), but Linux's
//    `vswprintf` interprets `%s` as `char*`.  Calling it with the
//    wrong-typed args dereferences pointers as bytes and segfaults.
//
// The trace output (`_DebugStringW(...)`) that triggers the crash on
// the addScript happy-path is debug-only — silencing it has zero
// functional impact.  So we:
//   • Pre-resolve every transitive header while __APPLE__ is unset
//     (so types.h dispatch picks linux-amd64).
//   • Replace `::vswprintf` with a no-op stub that returns 0.
//   • Flip __APPLE__ and pull in the upstream body — only StringW.cpp's
//     own line 300 `#elif __APPLE__` checks the flag, so the rest of
//     the source compiles unchanged.

// 1) Force-resolve every transitive header while __APPLE__ is unset.
#include <bfc/wasabi_std.h>
#include <bfc/std_mem.h>
#include <bfc/nsguid.h>
#include <bfc/string/StringW.h>

// 2) Stub vswprintf (Linux's interprets %s as char* not wchar_t*).
#include <cstdarg>
#include <cwchar>
static int wq_vswprintf(wchar_t *buf, std::size_t cap,
                        const wchar_t * /*fmt*/, std::va_list /*ap*/) {
    if (buf && cap > 0) buf[0] = 0;
    return 0;
}
#define vswprintf wq_vswprintf

// 3) Flip and pull the .cpp body.
#define __APPLE__ 1
#include <bfc/string/StringW.cpp>
#undef  __APPLE__
#undef  vswprintf
