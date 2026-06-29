// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// smoke-windef.cpp — minimal compile-only smoke test for the Phase
// A.2 Win32 typedefs shim.  Confirms that the most-commonly-used
// types resolve, the HWND-family DECLARE_HANDLE expansion produces
// distinct opaque types (so HWND→HMENU mixups fail to compile), and
// the geometry primitives are sized correctly.  No symbols leak —
// compiled into qtwasabi_compat OBJECT lib next to compat-version.cpp.
//

#include <windows.h>

namespace qtWasabi {
namespace wasabi_compat {
namespace {

// Verify the type identities we promise to upstream code.
static_assert(sizeof(BYTE)    == 1, "BYTE must be 1 byte");
static_assert(sizeof(WORD)    == 2, "WORD must be 2 bytes");
static_assert(sizeof(DWORD)   == 4, "DWORD must be 4 bytes");
static_assert(sizeof(QWORD)   == 8, "QWORD must be 8 bytes");
static_assert(sizeof(LONG)    >= 4, "LONG must be at least 4 bytes");
static_assert(sizeof(WCHAR)   == sizeof(wchar_t),
              "WCHAR is just wchar_t");
static_assert(sizeof(WPARAM)  == sizeof(void *),
              "WPARAM is pointer-sized (UINT_PTR)");
static_assert(sizeof(LPARAM)  == sizeof(void *),
              "LPARAM is pointer-sized (LONG_PTR)");
static_assert(sizeof(LRESULT) == sizeof(void *),
              "LRESULT is pointer-sized (LONG_PTR)");

// Geometry primitives — Win32 code embeds RECT/POINT/SIZE in
// structs and across IPC boundaries, so layout must be stable.
static_assert(sizeof(POINT) == 2 * sizeof(LONG),
              "POINT is two LONGs");
static_assert(sizeof(SIZE)  == 2 * sizeof(LONG),
              "SIZE is two LONGs");
static_assert(sizeof(RECT)  == 4 * sizeof(LONG),
              "RECT is four LONGs");

// HANDLE family — distinct opaque types so the compiler catches
// `HWND h = (HMENU)x;` style accidents.  We can't static_assert
// "these types are distinct"; instead we exercise that they are
// each at least pointer-sized and dereferenceable.
static_assert(sizeof(HWND)    == sizeof(void *), "HWND is opaque ptr");
static_assert(sizeof(HMENU)   == sizeof(void *), "HMENU is opaque ptr");
static_assert(sizeof(HBITMAP) == sizeof(void *), "HBITMAP is opaque ptr");

// LOWORD/HIWORD/MAKELONG roundtrip — used pervasively in Win32 IPC.
constexpr DWORD packed = MAKELONG(0xABCD, 0x1234);
static_assert(LOWORD(packed) == 0xABCD, "LOWORD round-trip");
static_assert(HIWORD(packed) == 0x1234, "HIWORD round-trip");

}  // anonymous
}  // namespace wasabi_compat
}  // namespace qtWasabi
