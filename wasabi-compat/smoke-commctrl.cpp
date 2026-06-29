// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// smoke-commctrl.cpp — compile-and-link smoke for the
// common-controls shim.  Exercises the struct field layout (so an
// upstream `LVITEMW lv = {LVIF_TEXT, …}` initialiser still
// compiles), the message-constant numerical values (sanity:
// LVM_FIRST=0x1000, TVM_FIRST=0x1100, HDM_FIRST=0x1200), and the
// ImageList stub round-trip.
//

#include "win32/commctrl.h"
#include "win32/winuser.h"

#include "smoke-check.h"

namespace qtWasabi {
namespace wasabi_compat {
namespace {

// Message-base sanity.  These match the Win32 SDK; mismatches
// would break wire-payload compatibility with upstream gen_ml
// code that hardcodes the offsets.  Note Win32's TreeView family
// uses `TV_FIRST` (not `TVM_FIRST`) as the base — the per-message
// constants are then TV_FIRST + offset.
static_assert(LVM_FIRST == 0x1000, "ListView msg base");
static_assert(TV_FIRST  == 0x1100, "TreeView msg base");
static_assert(HDM_FIRST == 0x1200, "Header msg base");

// Struct-layout sanity.  gen_ml allocates these on the stack and
// passes pointers across the SendMessage boundary; field order
// must match its expectations.
static_assert(offsetof(LVITEMW,  mask)     == 0,
              "LVITEMW.mask is first field");
static_assert(offsetof(TVITEMW,  mask)     == 0,
              "TVITEMW.mask is first field");
static_assert(offsetof(HDITEMW,  mask)     == 0,
              "HDITEMW.mask is first field");
static_assert(offsetof(NMHDR,    hwndFrom) == 0,
              "NMHDR.hwndFrom is first field");
static_assert(offsetof(NMTREEVIEWW, hdr)   == 0,
              "Notification body starts with NMHDR");

// ImageList stub round-trip.
struct CommCtrlSmoke {
    CommCtrlSmoke() {
        HIMAGELIST il = ImageList_Create(16, 16,
                                          ILC_COLOR32 | ILC_MASK,
                                          /*initial*/ 4, /*grow*/ 4);
        SMOKE_CHECK(il);
        // No real bitmap yet — Add returns 0 (the slot index) on the
        // first call, 1 on the second, etc.
        SMOKE_CHECK(ImageList_Add(il, nullptr, nullptr) == 0);
        SMOKE_CHECK(ImageList_Add(il, nullptr, nullptr) == 1);
        SMOKE_CHECK(ImageList_GetImageCount(il) == 2);
        IMAGEINFO ii;
        SMOKE_CHECK(ImageList_GetImageInfo(il, 0, &ii));
        SMOKE_CHECK(ii.rcImage.right == 16);
        SMOKE_CHECK(ii.rcImage.bottom == 16);
        SMOKE_CHECK(ImageList_Remove(il, 0));
        SMOKE_CHECK(ImageList_GetImageCount(il) == 1);
        SMOKE_CHECK(ImageList_Destroy(il));
    }
};
static CommCtrlSmoke s_smoke;

}  // anonymous
}  // namespace wasabi_compat
}  // namespace qtWasabi
