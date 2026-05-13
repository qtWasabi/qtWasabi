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

#include <QFont>
#include <QFontMetrics>
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
// Widget lookup is case-insensitive — Wasabi's findObject matches ids
// without regard to case, and Maki scripts mix case freely (XML has
// `id="Songticker"` while scripts call `findObject("songticker")`).
// Store under the lowercased id so a case-insensitive QHash lookup
// requires no extra QString allocation per probe.
QHash<QString, WidgetEntry> g_byId;

// Synthetic "main layout" handle for getParentLayout().  Set by
// SkinRuntime once per loadScripts pass; dropped on destroyAll().
// titlebar.m's resizeObjects calls `getParentLayout().getWidth()`
// expecting the layout's full width (e.g. 354 for the player normal
// layout) — without this pseudo it would get the calling widget's
// own width and centre the title against the wrong rectangle.
void *g_layoutRootScriptObject = nullptr;

// Per-script owner-widget handle, keyed by sid.  Populated by
// SkinRuntime::loadScripts from each ScriptRef's ownerGroupId.
// wq_getScriptGroup reads this back keyed on the currently-dispatching
// sid so scripts get their enclosing <group> rather than the
// SystemObject.
QHash<int, void *> g_scriptOwner;

// Registered by SkinView so script mutations trigger a repaint.
std::function<void()> g_repaint;

// Registered by SkinView so Maki scripts can query the host's
// playback state via getStatus().  Maki convention: 1=playing,
// -1=paused, 0=stopped.  Default returns 0 so scripts that gate
// behaviour on play state get the safe "stopped" reading.
std::function<int()> g_playbackStatus;

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
    if (!id.isEmpty()) g_byId.insert(id.toLower(), {w, scriptObjectHandle});
    if (!w->instanceId.isEmpty() && w->instanceId != id)
        g_byId.insert(w->instanceId.toLower(), {w, scriptObjectHandle});
}

void clearWidgetRegistry() {
    g_byId.clear();
    g_layoutRootScriptObject = nullptr;
    g_scriptOwner.clear();
}

void setLayoutRootScriptObject(void *handle) {
    g_layoutRootScriptObject = handle;
}

void setScriptOwnerWidget(int sid, void *scriptObjectHandle) {
    if (!scriptObjectHandle) g_scriptOwner.remove(sid);
    else                     g_scriptOwner.insert(sid, scriptObjectHandle);
}

void *scriptOwnerScriptObject(int sid) {
    auto it = g_scriptOwner.constFind(sid);
    return it == g_scriptOwner.constEnd() ? nullptr : it.value();
}

// SkinView calls this so script-side mutations of widget attrs can
// kick a repaint.  Pass nullptr to disable.
void registerSkinRepaintCallback(std::function<void()> cb) {
    g_repaint = std::move(cb);
}

// SkinView calls this so Maki getStatus() reads through to the host.
void registerSkinPlaybackStatusCallback(std::function<int()> cb) {
    g_playbackStatus = std::move(cb);
}

int fireWidgetEvent(const QString &widgetId, const wchar_t *eventName) {
    if (widgetId.isEmpty() || !eventName) return 0;
    auto it = g_byId.constFind(widgetId.toLower());
    if (it == g_byId.constEnd()) return 0;
    return Maki::fireZeroArgEventOnObject(it->scriptObject, eventName);
}

}  // namespace WasabiQt

// ── Bridge accessors used by wasabi-port/maki-bindings.cpp ──────

extern "C" {

void *wq_widget_findById(const wchar_t *id) {
    if (!id) return nullptr;
    auto it = WasabiQt::g_byId.constFind(WasabiQt::fromWide(id).toLower());
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

// Auto-width for <text>/<songticker> widgets.  Mirrors Wasabi's
// Text::getPreferences(SUGGESTED_W) (Src/Wasabi/api/skin/widgets/
// text.cpp:421-427): per-segment text width + 4 (Wasabi convention)
// + 7 (Win32-GDI / Qt-QFontMetrics width difference at the matching
// pixel size for Arial Bold) = + 11.  Returns -1 to signal "not a
// text widget — caller should fall back to the declared `w`".
//
// The script-side `getAutoWidth` is what the titlebar uses to size
// the streak gap around the title; without this calculation the
// streaks misalign with the rendered text width.
int wq_widget_textWidth(void *handle) {
    if (!handle) return -1;
    void *opaque = WasabiQt::Maki::opaqueOf(handle);
    auto *w = static_cast<WasabiQt::Layout::ResolvedWidget *>(opaque);
    if (!w) return -1;
    if (w->tag != QStringLiteral("text") &&
        w->tag != QStringLiteral("songticker"))
        return -1;

    // Pick the visible string the same way TextPainter does.  We
    // can't run the embedder's DisplayResolver here (no Host
    // hookup at the bridge layer), so display= falls through to
    // default/text — close enough for the titlebar, which only uses
    // default + setXmlParam("text", ...).
    QString s = w->attrs.value(QStringLiteral("text"));
    if (s.isEmpty()) s = w->attrs.value(QStringLiteral("default"));
    if (s.isEmpty()) return -1;

    if (w->attrs.value(QStringLiteral("forceuppercase")) ==
        QStringLiteral("1"))
        s = s.toUpper();

    QFont f;
    const QString family = w->attrs.value(QStringLiteral("font"));
    if (!family.isEmpty()) f.setFamily(family);
    bool ok = false;
    const int fontsize = w->attrs.value(QStringLiteral("fontsize")).toInt(&ok);
    if (ok && fontsize > 0)
        f.setPixelSize(qMax(1, (fontsize * 5 + 3) / 7));
    if (w->attrs.value(QStringLiteral("bold")) == QStringLiteral("1"))
        f.setBold(true);

    QFontMetrics fm(f);
    return fm.horizontalAdvance(s) + 9;
}

// Hand back the synthetic main-layout handle.  Returns nullptr if
// no skin has been loaded yet, in which case the caller falls back
// to the script's own widget (preserving the previous behaviour).
void *wq_layout_root() {
    return WasabiQt::g_layoutRootScriptObject;
}

// Lookup the owner widget for `sid`.  Returns nullptr if no owner
// was registered (e.g. for top-level scripts), in which case the
// caller falls back to the layout root or the receiver.
void *wq_script_owner(int sid) {
    return WasabiQt::scriptOwnerScriptObject(sid);
}

// Host playback status (1 playing, -1 paused, 0 stopped) — used by
// Maki's getStatus().  Returns 0 when no callback has been registered.
int wq_playback_status() {
    return WasabiQt::g_playbackStatus ? WasabiQt::g_playbackStatus() : 0;
}

}  // extern "C"
