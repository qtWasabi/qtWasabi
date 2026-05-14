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
#include <WasabiQt/Widget.h>

#include "../wasabi-port/maki-bridge.h"

#include <QEasingCurve>
#include <QFont>
#include <QFontMetrics>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantAnimation>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <functional>
#include <string>
#include <unordered_set>

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

// Registered by SkinView so Maki Layout.setTarget* / gotoTarget can
// resize the host window.  Drawer scripts use this pattern to grow /
// shrink the player when the upper drawer opens; without a callback,
// the layout's `h` attribute gets mutated by setTargetH but the
// SkinView keeps its old size and the chrome paints clipped.
std::function<void(int, int)> g_skinResize;
// Pending target values stashed by setTarget{W,H,X,Y} before
// gotoTarget consumes them.
int g_targetW = -1, g_targetH = -1, g_targetX = -1, g_targetY = -1;
// Set by an embedder's animated-resize handler before its tween
// completes — suppresses the synchronous onTargetReached firing the
// bridge would otherwise do, so the embedder can fire it itself when
// the tween lands.
bool g_animatedResizePending = false;

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

// C++-side counterpart to `wq_widget_findById` — used by Widget
// subclasses (e.g. MenuWidget) to resolve sibling references.
Widget *findWidgetById(const QString &id) {
    auto it = g_byId.constFind(id.toLower());
    if (it == g_byId.constEnd()) return nullptr;
    return it->widget;
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

void registerSkinResizeCallback(std::function<void(int, int)> cb) {
    g_skinResize = std::move(cb);
}

void beginAnimatedResize() {
    g_animatedResizePending = true;
}

void fireTargetReached() {
    if (!g_layoutRootScriptObject) return;
    g_animatedResizePending = false;
    Maki::fireZeroArgEventOnObject(
        g_layoutRootScriptObject, L"onTargetReached");
}

int fireWidgetEvent(const QString &widgetId, const wchar_t *eventName) {
    if (widgetId.isEmpty() || !eventName) return 0;
    auto it = g_byId.constFind(widgetId.toLower());
    if (it == g_byId.constEnd()) return 0;
    return Maki::fireZeroArgEventOnObject(it->scriptObject, eventName);
}

bool fireWidgetAttrSet(const QString &widgetId,
                       const QString &name,
                       const QString &value) {
    if (widgetId.isEmpty() || name.isEmpty()) return false;
    auto it = g_byId.constFind(widgetId.toLower());
    if (it == g_byId.constEnd()) return false;
    void *opaque = WasabiQt::Maki::opaqueOf(it->scriptObject);
    auto *w = static_cast<Layout::ResolvedWidget *>(opaque);
    if (!w) return false;
    w->attrs.insert(name, value);
    if (g_repaint) g_repaint();
    return true;
}

int fireWidgetActionEvent(const QString &targetId,
                          const QString &action,
                          const QString &param,
                          int x, int y, int p1, int p2,
                          const QString &sourceId) {
    if (targetId.isEmpty() || action.isEmpty()) return 0;
    auto it = g_byId.constFind(targetId.toLower());
    if (it == g_byId.constEnd()) return 0;
    void *src = nullptr;
    if (!sourceId.isEmpty()) {
        auto sit = g_byId.constFind(sourceId.toLower());
        if (sit != g_byId.constEnd()) src = sit->scriptObject;
    }
    // Hold the QString backings alive for the duration of the call —
    // toStdWString() returns by value; we need stable wchar_t* pointers
    // because the fired script reads its args lazily via the VCPU stack.
    std::wstring aBuf = action.toStdWString();
    std::wstring pBuf = param.toStdWString();
    return Maki::fireOnActionEvent(it->scriptObject,
                                    aBuf.c_str(), pBuf.c_str(),
                                    x, y, p1, p2, src);
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
    // Refuse to hide the active layout root.  Wasabi models each
    // container as having its own per-container Layout object;
    // qtWasabi's Maki bindings (wq_getContainer / wq_getLayoutByName)
    // currently lack that model and hand back the active layout root
    // for any container name.  When a script does
    // `getContainer("OtherContainer").getLayout("normal").hide()`
    // (Winamp Modern's playlistpro.m does this for the searchresults
    // popup window), the misdirected call lands on the player
    // root and the entire player vanishes.  Drop the mutation until
    // proper per-container layout tracking lands.
    if (handle == WasabiQt::g_layoutRootScriptObject &&
        std::wcscmp(name, L"visible") == 0 &&
        value && std::wcscmp(value, L"0") == 0) {
        if (std::getenv("WASABIQT_TRACE_LAYOUTROOT"))
            std::fprintf(stderr,
                "[wq_widget_setAttr] refusing to hide active layout root\n");
        return;
    }
    // The handle is a WidgetScriptObject*.  We get its opaque widget
    // ptr via the public bridge (avoids re-including the opensourced
    // ScriptObject header here).
    //
    // Routes through Widget::setXmlParam (virtual) so subclasses can
    // capture changes to typed state members alongside the attrs
    // hash — see ComponentBucketWidget::setXmlParam for the
    // canonical example (`_scroll` / `_entry_step` shadowed onto
    // typed ints for paint + hit-test).
    void *opaque = WasabiQt::Maki::opaqueOf(handle);
    auto *w = static_cast<WasabiQt::Widget *>(opaque);
    if (!w) return;

    // Refuse to hide widgets whose visibility is gated on a Maki
    // event chain that we don't drive yet.  Wasabi's videoavs.m
    // hides buttons.vis / buttons.video on initDrawer (via the
    // drawer.m onScriptLoaded path) and only re-shows them when the
    // user picks a mode via onShowVis / onShowVideo.  That re-show
    // is triggered through a Timer-backed callback chain
    // (drawer_dc_showVis → __callbackTimer.onTimer → drawer_showVis
    // → onShowVis) which doesn't survive our partial Timer port —
    // so the user-visible result is "vis drawer opens, but no Prev
    // / Next / Random / Detach / Switch buttons render".  Keep
    // these visible until the Timer-driven mode-switch is wired.
    static const std::unordered_set<std::wstring> kRefuseHide = {
        L"buttons.vis",
        L"buttons.vis.switchto",
        L"buttons.vis.detach",
        L"buttons.video",
        L"buttons.video.switchto",
        L"buttons.video.detach",
    };
    if (std::wcscmp(name, L"visible") == 0 &&
        value && std::wcscmp(value, L"0") == 0 &&
        !w->id.isEmpty() &&
        kRefuseHide.count(w->id.toStdWString())) {
        if (std::getenv("WASABIQT_TRACE_HIDE"))
            std::fprintf(stderr,
                "[wq_widget_setAttr] refusing hide on %s "
                "(Timer-gated re-show not wired)\n",
                w->id.toLocal8Bit().constData());
        return;
    }

    w->setXmlParam(WasabiQt::fromWide(name), WasabiQt::fromWide(value));
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

void wq_layout_set_target_w(int w) { WasabiQt::g_targetW = w; }
void wq_layout_set_target_h(int h) { WasabiQt::g_targetH = h; }
void wq_layout_set_target_x(int x) { WasabiQt::g_targetX = x; }
void wq_layout_set_target_y(int y) { WasabiQt::g_targetY = y; }

namespace {
// Owner QObject for QVariantAnimation instances driving per-widget
// setTarget animations.  Static — survives the program's lifetime.
QObject *widgetAnimOwner() {
    static QObject *o = new QObject();
    return o;
}
// Track the current animation per widget handle so a fresh
// gotoTarget cancels the in-flight one (drawer close right after
// drawer open mid-tween should re-target, not stack).
QHash<void *, QPointer<QVariantAnimation>> &widgetAnimMap() {
    static QHash<void *, QPointer<QVariantAnimation>> m;
    return m;
}
// Reference count of currently-running widget animations.  Read by
// embedders (SkinQuickItem) to suspend auto-fit so the QQuickWindow
// doesn't get resized on every tween frame — Wayland surface
// reconfigures per frame are expensive and visibly lag behind the
// animation, leaving the chrome clipped short of the target extent.
int g_widgetAnimsActive = 0;
}  // namespace

// Read-side accessor exposed in the public WasabiQt namespace (its
// declaration lives in SkinRuntime.h, not in any bridge header).
// Defined further down at file scope, outside the extern "C" block,
// so the symbol gets the expected C++ mangling.

// Animate a widget's x/y/w/h attrs from their current values to the
// supplied targets over `durationMs`.  -1 in any axis means "leave
// alone".  Fires `onTargetReached` on the widget's script object on
// completion.  Called by maki-bindings.cpp's wq_gotoTarget for any
// receiver that isn't the layout root — drawer.m / configtabs.m
// drive the config-drawer open/close slide through this path.
void wq_widget_animate_target(void *handle, int tx, int ty, int tw, int th,
                              int durationMs) {
    if (!handle) return;
    void *opaque = WasabiQt::Maki::opaqueOf(handle);
    auto *w = static_cast<WasabiQt::Layout::ResolvedWidget *>(opaque);
    if (!w) return;

    const int sx = w->attrs.value(QStringLiteral("x")).toInt();
    const int sy = w->attrs.value(QStringLiteral("y")).toInt();
    const int sw = w->attrs.value(QStringLiteral("w")).toInt();
    const int sh = w->attrs.value(QStringLiteral("h")).toInt();
    const bool noChange =
        (tx == -1 || tx == sx) && (ty == -1 || ty == sy) &&
        (tw == -1 || tw == sw) && (th == -1 || th == sh);
    if (noChange || durationMs <= 0) {
        // Snap — no change actually needed (or animation suppressed).
        // Set whatever's specified, fire onTargetReached, done.
        wchar_t buf[32];
        if (tx != -1) {
            std::swprintf(buf, 32, L"%d", tx);
            wq_widget_setAttr(handle, L"x", buf);
        }
        if (ty != -1) {
            std::swprintf(buf, 32, L"%d", ty);
            wq_widget_setAttr(handle, L"y", buf);
        }
        if (tw >= 0) {
            std::swprintf(buf, 32, L"%d", tw);
            wq_widget_setAttr(handle, L"w", buf);
        }
        if (th >= 0) {
            std::swprintf(buf, 32, L"%d", th);
            wq_widget_setAttr(handle, L"h", buf);
        }
        WasabiQt::Maki::fireZeroArgEventOnObject(handle, L"onTargetReached");
        return;
    }

    // Cancel any in-flight animation on this widget.
    auto &m = widgetAnimMap();
    auto it = m.find(handle);
    if (it != m.end()) {
        if (it.value()) it.value()->stop();
        m.erase(it);
        // Decrement: stop() doesn't emit finished, the running counter
        // would otherwise drift up forever as drawer open/close cancel
        // each other mid-tween.
        if (g_widgetAnimsActive > 0) --g_widgetAnimsActive;
    }

    auto *anim = new QVariantAnimation(widgetAnimOwner());
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setDuration(durationMs);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    ++g_widgetAnimsActive;
    QObject::connect(anim, &QVariantAnimation::valueChanged,
        widgetAnimOwner(),
        [handle, w, sx, sy, sw, sh, tx, ty, tw, th](const QVariant &v) {
            const double t = v.toDouble();
            auto setI = [&](const QString &k, int s, int e) {
                int cur = s + int((e - s) * t);
                w->attrs.insert(k, QString::number(cur));
            };
            if (tx != -1) setI(QStringLiteral("x"), sx, tx);
            if (ty != -1) setI(QStringLiteral("y"), sy, ty);
            if (tw >= 0) setI(QStringLiteral("w"), sw, tw);
            if (th >= 0) setI(QStringLiteral("h"), sh, th);
            if (WasabiQt::g_repaint) WasabiQt::g_repaint();
        });
    QObject::connect(anim, &QVariantAnimation::finished,
        widgetAnimOwner(), [handle]() {
            widgetAnimMap().remove(handle);
            if (g_widgetAnimsActive > 0) --g_widgetAnimsActive;
            if (::getenv("WASABIQT_TRACE_MAKI"))
                fprintf(stderr,
                    "[maki] widget anim finished, fire onTargetReached "
                    "handle=%p\n", handle);
            // Fire the target-reached event the configtabs/drawer
            // scripts listen on (sets DrawerOpen flag etc.).
            WasabiQt::Maki::fireZeroArgEventOnObject(handle, L"onTargetReached");
        });
    m[handle] = anim;
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

// gotoTarget — apply the stashed setTarget* values.  We don't animate
// (real Wasabi uses setTargetSpeed); the embedder gets the final size
// immediately.  Both w and h fall back to the current layout root w/h
// when un-set so a script that only changes one dimension doesn't
// accidentally collapse the other.
void wq_layout_goto_target() {
    if (!WasabiQt::g_skinResize) {
        WasabiQt::g_targetW = WasabiQt::g_targetH = -1;
        WasabiQt::g_targetX = WasabiQt::g_targetY = -1;
        return;
    }
    int w = WasabiQt::g_targetW;
    int h = WasabiQt::g_targetH;
    // Fall back to the layout root's current attrs when un-set.
    if (w < 0 || h < 0) {
        if (auto *root = static_cast<WasabiQt::Layout::ResolvedWidget *>(
                WasabiQt::Maki::opaqueOf(WasabiQt::g_layoutRootScriptObject))) {
            if (w < 0) w = root->attrs.value(QStringLiteral("w")).toInt();
            if (h < 0) h = root->attrs.value(QStringLiteral("h")).toInt();
        }
    }
    if (::getenv("WASABIQT_TRACE_MAKI"))
        ::fprintf(stderr, "[maki] gotoTarget w=%d h=%d (raw target=%d,%d)\n",
                  w, h, WasabiQt::g_targetW, WasabiQt::g_targetH);
    if (w > 0 && h > 0) WasabiQt::g_skinResize(w, h);
    WasabiQt::g_targetW = WasabiQt::g_targetH = -1;
    WasabiQt::g_targetX = WasabiQt::g_targetY = -1;
    // Fire onTargetReached only if the embedder's resize callback
    // didn't claim it'd fire it later (the animated path sets this
    // flag while it's still tweening, then clears + fires on tick
    // completion via wq_fire_target_reached).
    if (WasabiQt::g_animatedResizePending) return;

    // Synthesise the animation-complete event the script expects.
    // drawer.m's __main.onTargetReached() reads __drawer_direction
    // and fires onDoneOpeningDrawer / onDoneClosingDrawer — the close
    // chain in turn hides AVSGroup so the chrome doesn't render
    // through it once the window has shrunk back.  Real Wasabi fires
    // this when its animation thread completes; the default snap
    // path fires immediately.
    if (WasabiQt::g_layoutRootScriptObject) {
        WasabiQt::Maki::fireZeroArgEventOnObject(
            WasabiQt::g_layoutRootScriptObject, L"onTargetReached");
    }
}

// Embedder-driven async-resize completion.  An animated resize
// callback sets `g_animatedResizePending = true` synchronously when
// it starts the tween, suppressing the bridge's auto-fired
// onTargetReached.  When the tween finishes, the embedder calls this
// to deliver the event the scripts expect.
void wq_fire_target_reached() {
    if (!WasabiQt::g_layoutRootScriptObject) return;
    WasabiQt::g_animatedResizePending = false;
    WasabiQt::Maki::fireZeroArgEventOnObject(
        WasabiQt::g_layoutRootScriptObject, L"onTargetReached");
}

}  // extern "C"

namespace WasabiQt {
int widgetAnimationsActive() { return ::g_widgetAnimsActive; }
}  // namespace WasabiQt
