// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// handle-registry.cpp — thread-safe HWND/HMENU/HBITMAP/HMODULE/
// HIMAGELIST/HICON → backing-object map for wasabi-compat.
//
// Implementation strategy:
//   * One std::unordered_map per handle type, keyed by integer id.
//   * Handle value = encoded `(generation << 32) | id`, so a stale
//     handle whose slot has been recycled compares unequal at
//     lookup time (returns null, no use-after-free).
//   * Single mutex per type.  Lookup hot path: lock + map.find +
//     unlock.  Hash hit is microseconds; never contended unless
//     a worker thread is racing the GUI thread mid-SendMessage.
//
// All handle values are pointer-sized (UINT_PTR) which fits Win32
// expectations.  We cast through a uintptr_t intermediate so the
// opaque-struct pointer type that DECLARE_HANDLE produces gets
// reinterpreted to a value type without UB.
//

#include "handle-registry.h"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace qtWasabi {
namespace wasabi_compat {

// ── Default WindowObject method bodies ──────────────────────────
LRESULT WindowObject::wndProc(UINT /*msg*/, WPARAM /*wp*/, LPARAM /*lp*/) {
    // Default: "I didn't handle this."  Win32 convention is that
    // the framework wndProc returns 0 for unrecognised messages.
    // Concrete subclasses override and DefWindowProc-equivalent is
    // a no-op for our purposes (no system-managed nonclient area).
    return 0;
}

void WindowObject::paint(QPainter * /*p*/) {
    // Default: paint nothing.  Subclasses that own pixels override.
}

// ── Per-type table ──────────────────────────────────────────────
template <typename T>
struct HandleTable {
    std::mutex                                              mu;
    std::unordered_map<uint32_t, std::unique_ptr<T>>        slots;
    std::atomic<uint32_t>                                   next_id{1};
    std::atomic<uint32_t>                                   generation{1};

    static HandleTable<T> &instance() {
        static HandleTable<T> t;
        return t;
    }
};

// Handle encoding: top 32 bits = generation, bottom 32 = slot id.
// This catches stale-handle dereferences when a slot id is reused
// after destruction (generation bumps on every register).
static inline UINT_PTR encodeHandle(uint32_t gen, uint32_t id) {
    return (static_cast<UINT_PTR>(gen) << 32) |
            static_cast<UINT_PTR>(id);
}
static inline uint32_t handleGen(UINT_PTR h) {
    return static_cast<uint32_t>(h >> 32);
}
static inline uint32_t handleId(UINT_PTR h) {
    return static_cast<uint32_t>(h & 0xFFFFFFFFu);
}

// Map an opaque DECLARE_HANDLE pointer to / from UINT_PTR.  The
// `DECLARE_HANDLE(name)` macro makes `name` a `struct name__ *`,
// distinct per type.  reinterpret_cast through uintptr_t is the
// portable round-trip.
template <typename HType>
static inline HType encodeToHandle(UINT_PTR raw) {
    return reinterpret_cast<HType>(static_cast<uintptr_t>(raw));
}
template <typename HType>
static inline UINT_PTR decodeFromHandle(HType h) {
    return static_cast<UINT_PTR>(reinterpret_cast<uintptr_t>(h));
}

// ── Per-type self_handle setter helpers ─────────────────────────
//
// Each backing class carries a `self_handle` member of its own
// HType.  These small templates set it without the backing class
// needing to know about the registry's encoding.

static inline void setSelf(WindowObject    *o, HWND       h) { o->self_handle = h; }
static inline void setSelf(MenuObject      *o, HMENU      h) { o->self_handle = h; }
static inline void setSelf(BitmapObject    *o, HBITMAP    h) { o->self_handle = h; }
static inline void setSelf(ModuleObject    *o, HMODULE    h) { o->self_handle = h; }
static inline void setSelf(ImageListObject *o, HIMAGELIST h) { o->self_handle = h; }
static inline void setSelf(IconObject      *o, HICON      h) { o->self_handle = h; }

// ── Public API ──────────────────────────────────────────────────

template <typename T>
typename HandleTraits<T>::HType registerHandle(std::unique_ptr<T> backing) {
    using HType = typename HandleTraits<T>::HType;
    if (!backing) return nullptr;
    auto &table = HandleTable<T>::instance();
    const uint32_t id  = table.next_id.fetch_add(1);
    const uint32_t gen = table.generation.fetch_add(1);
    const UINT_PTR raw = encodeHandle(gen, id);
    HType handle = encodeToHandle<HType>(raw);
    T *raw_backing = backing.get();
    setSelf(raw_backing, handle);
    {
        std::lock_guard<std::mutex> lk(table.mu);
        table.slots.emplace(id, std::move(backing));
    }
    return handle;
}

template <typename T>
T *lookupHandle(typename HandleTraits<T>::HType handle) {
    using HType = typename HandleTraits<T>::HType;
    if (!handle) return nullptr;
    auto &table = HandleTable<T>::instance();
    const UINT_PTR raw = decodeFromHandle<HType>(handle);
    const uint32_t id  = handleId(raw);
    std::lock_guard<std::mutex> lk(table.mu);
    auto it = table.slots.find(id);
    if (it == table.slots.end()) return nullptr;
    // Re-check the encoded handle matches our slot's owner — guards
    // the stale-after-recycle case (id reused, generation differs).
    if (decodeFromHandle<HType>(it->second->self_handle) != raw)
        return nullptr;
    return it->second.get();
}

template <typename T>
void unregisterHandle(typename HandleTraits<T>::HType handle) {
    using HType = typename HandleTraits<T>::HType;
    if (!handle) return;
    auto &table = HandleTable<T>::instance();
    const UINT_PTR raw = decodeFromHandle<HType>(handle);
    const uint32_t id  = handleId(raw);
    std::lock_guard<std::mutex> lk(table.mu);
    auto it = table.slots.find(id);
    if (it == table.slots.end()) return;
    if (decodeFromHandle<HType>(it->second->self_handle) != raw) return;
    table.slots.erase(it);
}

// ── Explicit instantiations ─────────────────────────────────────
//
// The .h declares these as `extern template`; here we define them
// so all six type families share a single TU's worth of object
// code rather than re-emitting per-caller.

template HWND       registerHandle<WindowObject>   (std::unique_ptr<WindowObject>);
template HMENU      registerHandle<MenuObject>     (std::unique_ptr<MenuObject>);
template HBITMAP    registerHandle<BitmapObject>   (std::unique_ptr<BitmapObject>);
template HMODULE    registerHandle<ModuleObject>   (std::unique_ptr<ModuleObject>);
template HIMAGELIST registerHandle<ImageListObject>(std::unique_ptr<ImageListObject>);
template HICON      registerHandle<IconObject>     (std::unique_ptr<IconObject>);

template WindowObject    *lookupHandle<WindowObject>   (HWND);
template MenuObject      *lookupHandle<MenuObject>     (HMENU);
template BitmapObject    *lookupHandle<BitmapObject>   (HBITMAP);
template ModuleObject    *lookupHandle<ModuleObject>   (HMODULE);
template ImageListObject *lookupHandle<ImageListObject>(HIMAGELIST);
template IconObject      *lookupHandle<IconObject>     (HICON);

template void unregisterHandle<WindowObject>   (HWND);
template void unregisterHandle<MenuObject>     (HMENU);
template void unregisterHandle<BitmapObject>   (HBITMAP);
template void unregisterHandle<ModuleObject>   (HMODULE);
template void unregisterHandle<ImageListObject>(HIMAGELIST);
template void unregisterHandle<IconObject>     (HICON);

}  // namespace wasabi_compat
}  // namespace qtWasabi
