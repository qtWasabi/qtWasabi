// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// commctrl-stubs.cpp — link-time bodies for the Common
// Controls helpers gen_ml + ml_* expect (`ImageList_*`,
// `InitCommonControls(Ex)`, …).
//
// Most are NO-OP stubs that return sensible "nothing here"
// defaults so gen_ml + ml_* link and boot without crashing.
// The TreeView/ListView state itself is hosted by qtWasabi's
// TreeListWidget / MultiColumnListWidget primitives, not here.
//
// Why ImageList gets a real registration entry now even with a
// no-op create: gen_ml's nav-control init wants a non-null
// HIMAGELIST handle to store in the tree.  A registered backing
// object means subsequent ImageList_AddIcon calls find a real
// receiver — the receiver just records the icon count for the
// renderer to consume.
//

#include "win32/commctrl.h"
#include "win32/handle-registry.h"

#include <QImage>
#include <QVector>

namespace qtWasabi {
namespace wasabi_compat {

// ImageList backing — real per-slot QImage storage.  Each
// Add/AddIcon call inserts a QImage into `images`; consumers
// (TreeListWidget icon resolver, ListView row paint) look up
// by index.  HICON / HBITMAP backings are resolved through the
// handle registry so an Add call that passes a real HICON gets
// its QImage extracted.
class ConcreteImageList : public ImageListObject {
public:
    int             cx = 0;
    int             cy = 0;
    int             count = 0;
    QVector<QImage> images;

    // Lookup by slot index; returns null QImage if out of range.
    QImage imageAt(int i) const {
        if (i < 0 || i >= images.size()) return QImage();
        return images[i];
    }
};

}  // namespace wasabi_compat
}  // namespace qtWasabi

extern "C" {

HIMAGELIST WINAPI ImageList_Create(int cx, int cy, UINT /*flags*/,
                                     int /*initial*/, int /*grow*/) {
    using namespace qtWasabi::wasabi_compat;
    auto il = std::make_unique<ConcreteImageList>();
    il->cx = cx;
    il->cy = cy;
    return registerHandle<ImageListObject>(std::move(il));
}

BOOL WINAPI ImageList_Destroy(HIMAGELIST himl) {
    using namespace qtWasabi::wasabi_compat;
    if (!himl) return FALSE;
    unregisterHandle<ImageListObject>(himl);
    return TRUE;
}

int WINAPI ImageList_Add(HIMAGELIST himl, HBITMAP /*hbmImage*/, HBITMAP /*hbmMask*/) {
    using namespace qtWasabi::wasabi_compat;
    ImageListObject *obj = lookupHandle<ImageListObject>(himl);
    if (!obj) return -1;
    auto *il = static_cast<ConcreteImageList *>(obj);
    // Extracting a QImage from the HBITMAP would need cross-TU
    // access to the ConcreteBitmap backing (declared inside
    // wingdi-stubs.cpp's anonymous namespace), so this slot holds a
    // placeholder transparent QImage and only the count is tracked.
    // Callers that already have decoded pixels should use
    // wasabi_compat_ImageList_AddQImage instead.
    il->images.append(QImage());
    return il->count++;
}

int WINAPI ImageList_AddIcon(HIMAGELIST himl, HICON /*hicon*/) {
    using namespace qtWasabi::wasabi_compat;
    ImageListObject *obj = lookupHandle<ImageListObject>(himl);
    if (!obj) return -1;
    auto *il = static_cast<ConcreteImageList *>(obj);
    il->images.append(QImage());
    return il->count++;
}

// Convenience entry — direct QImage upload.  Used by
// the qtwasabi-side bridges that already have decoded pixels
// (ml resource loader, plugin static-init icon caches) and want
// to skip the HICON / HBITMAP boilerplate.  Not part of the Win32
// API surface; not exported through commctrl.h.
extern "C" int wasabi_compat_ImageList_AddQImage(HIMAGELIST himl,
                                                   const QImage &img) {
    using namespace qtWasabi::wasabi_compat;
    ImageListObject *obj = lookupHandle<ImageListObject>(himl);
    if (!obj) return -1;
    auto *il = static_cast<ConcreteImageList *>(obj);
    il->images.append(img);
    return il->count++;
}

int WINAPI ImageList_GetImageCount(HIMAGELIST himl) {
    using namespace qtWasabi::wasabi_compat;
    ImageListObject *obj = lookupHandle<ImageListObject>(himl);
    if (!obj) return 0;
    return static_cast<ConcreteImageList *>(obj)->count;
}

BOOL WINAPI ImageList_GetImageInfo(HIMAGELIST himl, int /*i*/, IMAGEINFO *pii) {
    using namespace qtWasabi::wasabi_compat;
    if (!pii) return FALSE;
    ImageListObject *obj = lookupHandle<ImageListObject>(himl);
    if (!obj) return FALSE;
    auto *il = static_cast<ConcreteImageList *>(obj);
    pii->hbmImage = nullptr;
    pii->hbmMask  = nullptr;
    pii->Unused1  = 0;
    pii->Unused2  = 0;
    pii->rcImage  = {0, 0, il->cx, il->cy};
    return TRUE;
}

BOOL WINAPI ImageList_Remove(HIMAGELIST himl, int /*i*/) {
    using namespace qtWasabi::wasabi_compat;
    ImageListObject *obj = lookupHandle<ImageListObject>(himl);
    if (!obj) return FALSE;
    auto *il = static_cast<ConcreteImageList *>(obj);
    if (il->count > 0) il->count--;
    return TRUE;
}

BOOL WINAPI InitCommonControls(void) {
    // No-op for us — the common-control "classes" are always
    // available via the wasabi-compat handle registry.  Returning
    // TRUE keeps gen_ml's init path happy.
    return TRUE;
}

BOOL WINAPI InitCommonControlsEx(const INITCOMMONCONTROLSEX * /*picce*/) {
    return TRUE;
}

}  // extern "C"
