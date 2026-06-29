// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once
//
// handle-registry.h — thread-safe map from Win32 opaque handle
// (HWND, HMENU, HBITMAP, HMODULE, HICON, …) to a backing C++ object
// owned by wasabi-compat.
//
// Why a registry rather than just casting `BackingObject *` to
// HWND directly:
//   1. Type safety.  Win32 code does `HWND h = something; …
//      auto m = (HMENU)x;`.  A flat ptr-cast scheme conflates the
//      two; the registry's per-type tag catches the mistake at
//      runtime and returns null on a wrong-type lookup.
//   2. Lifecycle.  Win32 SendMessage handlers can re-enter and
//      destroy the receiving HWND mid-dispatch.  The registry
//      hands out generation-tagged handles so a stale pointer
//      lookup returns null instead of dereferencing freed memory.
//   3. Introspection.  A trace/log facility can walk every live
//      HWND to dump state — only possible with an authoritative
//      list.
//   4. Cross-thread access.  Win32 SendMessage from a worker
//      thread is legal; the registry's internal mutex makes the
//      lookup safe without forcing each backing class to be
//      thread-safe.
//
// Backing object lifetime is owned by the registry: register*()
// transfers ownership in.  unregister*() destroys.  The user
// never deletes the backing object directly.
//

#include "basetsd.h"
#include "windef.h"

#include <cstdint>
#include <memory>

// Global-namespace forward declaration so the WindowObject::paint
// signature names the real ::QPainter (not a namespace-scoped
// stand-in).  Subclasses in qtwasabi/ml/ implement paint() with
// `#include <QPainter>` and rely on this matching.
class QPainter;

namespace qtWasabi {
namespace wasabi_compat {

// ── Backing-object base classes ─────────────────────────────────
//
// Each Win32 handle type has its own backing-object class so the
// registry can refuse wrong-type lookups (HWND::lookup of an HMENU
// handle returns null) and so the per-type virtuals are stable.
//
// WindowObject is the most heavily used: every HWND eventually
// routes a SendMessage through `wndProc()`.  Subclasses implement
// the message switch.  Here the base just owns the message
// routing virtual.

class WindowObject;
class MenuObject;
class BitmapObject;
class ModuleObject;
class ImageListObject;
class IconObject;

// Trait used by the templated registry to distinguish backing
// types.  Specialised below for each handle family.
template <typename T> struct HandleTraits;

template <> struct HandleTraits<WindowObject>    { using HType = HWND;       static constexpr const char *name = "HWND"; };
template <> struct HandleTraits<MenuObject>      { using HType = HMENU;      static constexpr const char *name = "HMENU"; };
template <> struct HandleTraits<BitmapObject>    { using HType = HBITMAP;    static constexpr const char *name = "HBITMAP"; };
template <> struct HandleTraits<ModuleObject>    { using HType = HMODULE;    static constexpr const char *name = "HMODULE"; };
template <> struct HandleTraits<ImageListObject> { using HType = HIMAGELIST; static constexpr const char *name = "HIMAGELIST"; };
template <> struct HandleTraits<IconObject>      { using HType = HICON;      static constexpr const char *name = "HICON"; };

// ── Generic registry API ────────────────────────────────────────
//
// Implementation is per-type private inside handle-registry.cpp;
// the public surface is a small templated facade so callers can
// `registerHandle<WindowObject>(std::move(p))` and get back the
// matching HWND, etc.

template <typename T>
typename HandleTraits<T>::HType registerHandle(std::unique_ptr<T> backing);

template <typename T>
T *lookupHandle(typename HandleTraits<T>::HType handle);

template <typename T>
void unregisterHandle(typename HandleTraits<T>::HType handle);

// Explicit instantiations declared here, defined in the .cpp.
extern template HWND       registerHandle<WindowObject>   (std::unique_ptr<WindowObject>);
extern template HMENU      registerHandle<MenuObject>     (std::unique_ptr<MenuObject>);
extern template HBITMAP    registerHandle<BitmapObject>   (std::unique_ptr<BitmapObject>);
extern template HMODULE    registerHandle<ModuleObject>   (std::unique_ptr<ModuleObject>);
extern template HIMAGELIST registerHandle<ImageListObject>(std::unique_ptr<ImageListObject>);
extern template HICON      registerHandle<IconObject>     (std::unique_ptr<IconObject>);

extern template WindowObject    *lookupHandle<WindowObject>   (HWND);
extern template MenuObject      *lookupHandle<MenuObject>     (HMENU);
extern template BitmapObject    *lookupHandle<BitmapObject>   (HBITMAP);
extern template ModuleObject    *lookupHandle<ModuleObject>   (HMODULE);
extern template ImageListObject *lookupHandle<ImageListObject>(HIMAGELIST);
extern template IconObject      *lookupHandle<IconObject>     (HICON);

extern template void unregisterHandle<WindowObject>   (HWND);
extern template void unregisterHandle<MenuObject>     (HMENU);
extern template void unregisterHandle<BitmapObject>   (HBITMAP);
extern template void unregisterHandle<ModuleObject>   (HMODULE);
extern template void unregisterHandle<ImageListObject>(HIMAGELIST);
extern template void unregisterHandle<IconObject>     (HICON);

// ── Backing-object base classes ─────────────────────────────────
//
// Concrete subclasses live in the per-feature directories (e.g.
// the `WindowObject` subclasses for each Win32 window
// class).  Here we just declare the interface.

// WindowObject — backing for HWND inside wasabi-compat.
//
// Deliberately NOT a QWidget.  qtWasabi renders the whole skin
// through `SkinQuickItem::updatePaintNode → paintInto(QPainter&,
// QSize)` into a single QImage that becomes a QSG texture.
// Embedding QWidgets in that pipeline would force `QWidget::grab()`
// per frame (heavy) or `QQuickWidget` composition (different
// software node behaviour).  Instead, `WindowObject::paint()`
// uses the same `QPainter` the rest of the skin paints with, so
// gen_ml's HWND content composites into the same QImage as Bento's
// chrome with no extra textures or roundtrips.
//
// Mouse + keyboard events route IN via `wndProc(WM_*)` — the
// host's existing hit-test walks WindowObject's child rects and
// dispatches there.  A SendMessage from a worker thread must
// reach the GUI thread; each WindowObject's wndProc is
// responsible for marshalling there (the SendMessage dispatcher
// is where a Qt::BlockingQueuedConnection hop would live).
class WindowObject {
public:
    virtual ~WindowObject() = default;

    // Handle to self once registered.  Set by registerHandle().
    HWND  self_handle = nullptr;

    // Parent HWND, or null for top-level.
    HWND  parent = nullptr;

    // Win32 client rect in parent-relative coords.  When this
    // WindowObject is the root of a windowholder, the rect is the
    // holder's bbox in skin canvas coords.
    RECT  rect = {0, 0, 0, 0};

    // Win32 message dispatch.  Subclasses override to handle the
    // message classes they care about.  Default returns 0 (the
    // canonical "I didn't handle this" sentinel).
    virtual LRESULT wndProc(UINT msg, WPARAM wp, LPARAM lp);

    // Composite into the host's QPainter.  Called from the skin's
    // paint pass via the windowholder dispatch in
    // `WindowHolder::paintEmbeddedHwnd`.  The QPainter
    // is already translated to the WindowObject's parent-relative
    // origin; subclasses paint at (0,0) of `rect`'s size.
    virtual void paint(QPainter *p);

    // Win32 user-data slots: GWL_USERDATA, GWL_WNDPROC, etc.
    // gen_ml stores its `mainwndState *` here.
    LONG_PTR user_data[8] = {0};
};

class MenuObject {
public:
    virtual ~MenuObject() = default;
    HMENU self_handle = nullptr;
    // The menu-item list + dispatch are built out separately.
};

class BitmapObject {
public:
    virtual ~BitmapObject() = default;
    HBITMAP self_handle = nullptr;
    // The QImage backing is attached separately.
};

class ModuleObject {
public:
    virtual ~ModuleObject() = default;
    HMODULE self_handle = nullptr;
    // Used to back LoadLibraryW / GetProcAddress for the gen_ml
    // plugin-of-plugin model.
};

class ImageListObject {
public:
    virtual ~ImageListObject() = default;
    HIMAGELIST self_handle = nullptr;
};

class IconObject {
public:
    virtual ~IconObject() = default;
    HICON self_handle = nullptr;
};

}  // namespace wasabi_compat
}  // namespace qtWasabi
