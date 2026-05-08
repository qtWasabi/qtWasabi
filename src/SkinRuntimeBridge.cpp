// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// SkinRuntimeBridge.cpp — Qt-side bridge accessors for the
// maki-bindings.cpp method bodies.  Sole TU exposing widget-tree
// operations through a simple C ABI so wasabi-port's TUs (which
// avoid Qt to dodge bfc/platform/linux.h's macro pollution) can
// still mutate widgets.
//
// The convention: functions take/return `void *` for widget handles
// (`Layout::ResolvedWidget *`) and `const wchar_t *` for strings.
// All names are `extern "C"` so maki-bindings.cpp can declare them
// without including any Qt headers.

#include <WasabiQt/Layout.h>

#include "../wasabi-port/maki-bridge.h"

#include <QHash>
#include <QString>
#include <functional>

namespace WasabiQt {

namespace {
// Active widget map — set by SkinRuntime each time it (re)loads.
// Maps widget id → ResolvedWidget pointer + the ScriptObject handle
// the opensourced VM dispatches against.
struct WidgetEntry {
    Layout::ResolvedWidget *widget;
    void                   *scriptObject;   // WidgetScriptObject handle
};
QHash<QString, WidgetEntry> g_byId;

// Registered by SkinView so script mutations trigger a repaint.
std::function<void()> g_repaint;

QString fromWide(const wchar_t *s) {
    return s ? QString::fromWCharArray(s) : QString();
}
}  // namespace

// Called by SkinRuntime after every successful loadScripts pass.
// Registers the widget under both its `id` and (when present) its
// `instanceid` — Wasabi's findObject semantics treat instance names
// as first-class lookup keys.
void registerWidgetForScripts(const QString &id, Layout::ResolvedWidget *w,
                              void *scriptObjectHandle) {
    if (!w) return;
    if (!id.isEmpty()) g_byId.insert(id, {w, scriptObjectHandle});
    if (!w->instanceId.isEmpty() && w->instanceId != id)
        g_byId.insert(w->instanceId, {w, scriptObjectHandle});
}

void clearWidgetRegistry() {
    g_byId.clear();
}

// SkinView calls this so script-side mutations of widget attrs can
// kick a repaint.  Pass nullptr to disable.
void registerSkinRepaintCallback(std::function<void()> cb) {
    g_repaint = std::move(cb);
}

}  // namespace WasabiQt

// ── Bridge accessors used by wasabi-port/maki-bindings.cpp ──────

extern "C" {

void *wq_widget_findById(const wchar_t *id) {
    if (!id) return nullptr;
    auto it = WasabiQt::g_byId.constFind(WasabiQt::fromWide(id));
    return it == WasabiQt::g_byId.constEnd() ? nullptr : it->scriptObject;
}

void wq_widget_setAttr(void *handle, const wchar_t *name, const wchar_t *value) {
    if (!handle || !name) return;
    // The handle is a WidgetScriptObject*.  We get its opaque widget
    // ptr via the public bridge (avoids re-including the opensourced
    // ScriptObject header here).
    void *opaque = WasabiQt::Maki::opaqueOf(handle);
    auto *w = static_cast<WasabiQt::Layout::ResolvedWidget *>(opaque);
    if (!w) return;
    w->attrs.insert(WasabiQt::fromWide(name),
                     WasabiQt::fromWide(value));
    if (WasabiQt::g_repaint) WasabiQt::g_repaint();
}

const wchar_t *wq_widget_getAttr(void *handle, const wchar_t *name) {
    static thread_local QString cache;     // keep alive for the call
    if (!handle || !name) return L"";
    void *opaque = WasabiQt::Maki::opaqueOf(handle);
    auto *w = static_cast<WasabiQt::Layout::ResolvedWidget *>(opaque);
    if (!w) return L"";
    cache = w->attrs.value(WasabiQt::fromWide(name));
    static thread_local std::wstring buf;
    buf.assign(cache.toStdWString());
    return buf.c_str();
}

int wq_widget_getAttrInt(void *handle, const wchar_t *name) {
    if (!handle || !name) return 0;
    void *opaque = WasabiQt::Maki::opaqueOf(handle);
    auto *w = static_cast<WasabiQt::Layout::ResolvedWidget *>(opaque);
    if (!w) return 0;
    return w->attrs.value(WasabiQt::fromWide(name)).toInt();
}

}  // extern "C"
