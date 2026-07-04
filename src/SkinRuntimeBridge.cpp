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

#include <qtWasabi/BitmapRegistry.h>
#include <qtWasabi/Layout.h>
#include <qtWasabi/SkinRuntime.h>
#include <qtWasabi/TextPainter.h>
#include <qtWasabi/Widget.h>

#include "../wasabi-port/maki-bridge.h"

#include <QEasingCurve>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHash>
#include <QSet>
#include <QScreen>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantAnimation>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <functional>
#include <string>
#include <unordered_set>

namespace qtWasabi {

namespace {
// Active widget map — set by SkinRuntime each time it (re)loads.
// Maps widget id → ResolvedWidget pointer + the ScriptObject handle
// the Maki VM dispatches against.
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

// Reverse map widget → its ScriptObject handle.  g_byId is keyed by id
// and COLLIDES when a skin reuses an id (Bento's InfoLine groups each
// contain a child <Text id="text"> + <Text id="label">), so a flat
// id lookup can only return one of them.  This lets findObject() return
// the correct per-instance child by walking the receiver's subtree and
// mapping the matched widget back to its own script object.
QHash<const Layout::ResolvedWidget *, void *> g_scriptObjByWidget;

// Maki Timer objects (`new Timer` + setDelay/start/stop/onTimer) backed
// by real QTimers, keyed by the Timer ScriptObject handle.  Every
// Wasabi skin animation / ticker / deferred-init (Bento's playlist
// enlarge via dc_loadWnd.onTimer, the file-info cycler, drawer tweens)
// depends on these actually firing.
QHash<void *, QTimer *> g_makiTimers;

// The script root that OWNS each Maki Timer, captured when the backing
// QTimer is created (the first arm always happens from root-bracketed
// script execution).  The timeout callback re-enters the VM under this
// root: an async fire must never run under whichever window happens to
// be active — a player timer firing under the Playlist Editor's root
// resolves its byId lookups against the WRONG window's registry and
// writes garbage geometry (the "resizing the PL shifts the player's EQ
// drawer" corruption).
QHash<void *, const void *> g_timerRoot;

// Maki Timer semantics: the `started` flag is the source of truth for
// isRunning(); setDelay only re-arms IF already started; stop() clears
// started but keeps the QTimer so it can restart; the QTimer is repeating
// (Win32 SetTimer) — one-shot deferrals (Bento's dc_*/callbackTimer) work
// because the handler self-stops.
std::unordered_set<void *> g_timerStarted;

// Layout-settle flag.  Set true whenever a Maki write (setXmlParam or
// setPosition) changes a GEOMETRY-affecting attribute to a NEW value.
// SkinRuntime::dispatchInitialResize iterates the onResize cascade to a
// fixpoint on this flag, so a handler that reflows the tree settles like the
// Wasabi invalidate->onResize loop: pledit's enlarge grows the playlist
// column, then (column now tall) playlistpro.frameGroup.onResize reveals the
// "Search in Playlist" header and offsets the list.  Fully general — no skin
// ids, driven purely by which attrs the running scripts mutate.  Cleared each
// settle pass via clearGeometryDirty() — the accessor fns live below, OUTSIDE
// this anonymous namespace, so SkinRuntime.cpp links.
bool g_geometryDirty = false;
// The attrs whose mutation changes a widget's resolved rect (and thus the
// rects of its descendants), so an onResize re-fire is warranted.  `visible`
// is intentionally EXCLUDED: qtamp's model keeps a hidden widget's rect, so
// hiding a sibling never moves the others.
static bool isGeometryAttr(const wchar_t *name) {
    static const std::unordered_set<std::wstring> kGeomAttrs = {
        L"x", L"y", L"w", L"h",
        L"relatx", L"relaty", L"relatw", L"relath",
        L"from", L"maxwidth", L"minwidth", L"maxheight", L"minheight",
        L"position",
    };
    return name && kGeomAttrs.count(std::wstring(name)) != 0;
}

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

// External-linkage accessors for the geometry-dirty settle flag (the flag
// + isGeometryAttr live in the anonymous namespace above).  Called by
// SkinRuntime::dispatchInitialResize's onResize fixpoint loop.
void clearGeometryDirty() { g_geometryDirty = false; }
bool geometryDirty()      { return g_geometryDirty; }

// Bitmap registry pointer so the Maki bridge can resolve a
// `<layer image="…">`'s bound bitmap to its intrinsic pixel
// dimensions.  The Wasabi Layer autoWidth/Height preference is the
// bitmap's source-rect size — Bento's `mainmenu.maki` uses this to
// lay out the File/Play/Options/View/Help menu items sequentially.
// Set by SkinRuntime when scripts load; cleared on destroyAll.  In
// qtWasabi namespace (not anonymous) so the file-scope
// wq_widget_bitmapWidth bridge stub can reach it via
// `qtWasabi::g_bitmapRegistry`.
BitmapRegistry *g_bitmapRegistry = nullptr;

// ── Per-window (per-root) script context ───────────────────────────
// Real Wasabi runs ONE script engine but resolves objects relative to
// the dispatching window (guiobj.cpp: getRootWnd()->findWindow(id)).
// qtWasabi mirrors that: the swappable VM globals above (g_byId, the
// layout-root pseudo, repaint/resize callbacks, the resize target, the
// bitmap registry) are the *active root's* live data.  Each window
// (the player, plus each container subwindow) owns a RootData snapshot;
// switching the active root saves the current globals into the outgoing
// root and loads the incoming root's.
//
// The player is the PERMANENT resting root — for a single-window session
// the active root never changes, so switchActiveRoot is a no-op early
// return and none of this is exercised (byte-identical to before).  A
// subwindow brackets its load + dispatch + input with ScopedScriptRoot,
// which swaps its snapshot in and the player's back on scope exit.
namespace {
struct RootData {
    QHash<QString, WidgetEntry>           byId;
    QList<const Layout::ResolvedWidget *> registered;  // keys into g_scriptObjByWidget
    void                                 *layoutRoot = nullptr;
    std::function<void()>                 repaint;
    std::function<void(int, int)>         skinResize;
    int   targetW = -1, targetH = -1, targetX = -1, targetY = -1;
    bool  animatedResizePending = false;
    BitmapRegistry                       *bitmapRegistry = nullptr;
};
QHash<const void *, RootData> g_roots;
const void *g_activeRoot = nullptr;
// The active root's registered-widget list (lives alongside g_byId so the
// per-root teardown can drop exactly this root's g_scriptObjByWidget keys).
QList<const Layout::ResolvedWidget *> g_activeRegistered;

void saveActiveRoot() {
    if (!g_activeRoot) return;
    RootData &d = g_roots[g_activeRoot];
    d.byId       = std::move(g_byId);
    d.registered = std::move(g_activeRegistered);
    d.layoutRoot = g_layoutRootScriptObject;
    d.repaint    = g_repaint;
    d.skinResize = g_skinResize;
    d.targetW = g_targetW; d.targetH = g_targetH;
    d.targetX = g_targetX; d.targetY = g_targetY;
    d.animatedResizePending = g_animatedResizePending;
    d.bitmapRegistry = g_bitmapRegistry;
    g_byId.clear();
    g_activeRegistered.clear();
}
void loadActiveRoot(const void *key) {
    RootData &d = g_roots[key];   // default-constructs an empty root if new
    g_byId       = std::move(d.byId);
    g_activeRegistered = std::move(d.registered);
    g_layoutRootScriptObject = d.layoutRoot;
    g_repaint    = d.repaint;
    g_skinResize = d.skinResize;
    g_targetW = d.targetW; g_targetH = d.targetH;
    g_targetX = d.targetX; g_targetY = d.targetY;
    g_animatedResizePending = d.animatedResizePending;
    g_bitmapRegistry = d.bitmapRegistry;
    g_activeRoot = key;
}
void switchActiveRoot(const void *key) {
    if (key == g_activeRoot) return;
    if (!g_activeRoot) {
        // First activation: the current live globals were configured FOR
        // this incoming root (the embedder sets the bitmap registry +
        // repaint/resize callbacks before the first loadScripts), so adopt
        // them instead of loading an empty snapshot over them.
        g_activeRoot = key;
        return;
    }
    saveActiveRoot();
    loadActiveRoot(key);
}
}  // namespace

void  setActiveScriptRoot(const void *rootKey) { switchActiveRoot(rootKey); }
const void *activeScriptRoot()                 { return g_activeRoot; }
bool  scriptRootAlive(const void *rootKey) {
    return rootKey == g_activeRoot || g_roots.contains(rootKey);
}
ScopedScriptRoot::ScopedScriptRoot(const void *rootKey)
    : m_prev(g_activeRoot) { switchActiveRoot(rootKey); }
ScopedScriptRoot::~ScopedScriptRoot() { switchActiveRoot(m_prev); }

// Forget a window's per-root snapshot entirely (its window closed / its
// runtime was destroyed).  Caller must have already torn the root down
// (cleared its registry under ScopedScriptRoot); this just drops the
// now-empty snapshot so g_roots doesn't accumulate dead windows.  If the
// dropped root was active, fall back to "no active root" so the next
// dispatch re-establishes one (adopt-on-first-activation).
void dropScriptRoot(const void *rootKey) {
    g_roots.remove(rootKey);
    if (g_activeRoot == rootKey) g_activeRoot = nullptr;
}

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
    if (scriptObjectHandle) {
        g_scriptObjByWidget.insert(w, scriptObjectHandle);
        g_activeRegistered.append(w);   // for per-root teardown
    }
}

// Replay onTextChanged on every text widget that carries text + a bound
// handler.  The Wasabi Text setText path fires onTextChanged; the engine's
// static XUI-label injection (tagTexts in Layout.cpp) writes a label's text
// WITHOUT going through the VM, so a skin script's <label>.onTextChanged
// (Bento infoline.maki positions the value field after the label via it)
// never runs and the value overlaps the label.  Call once after
// dispatchOnScriptLoaded — the per-widget script objects (created on demand
// by findObject) and their handlers exist by then.  No-op for widgets without
// an onTextChanged handler.
int fireStaticTextChanged() {
    int fired = 0;
    // Per-root scoping, same rationale as firePerObjectResize: the replay
    // after dispatchOnScriptLoaded targets the just-loaded window's labels.
    // Iterating the global widget→scriptObject map fired it on EVERY
    // window's text widgets and, worse, dereferenced dangling keys: an
    // entry registered under root A for a widget owned by root B's tree
    // (cross-window findObject) survives B's teardown, and the next skin
    // switch walked straight into the freed widget (the Preferences
    // skin-change SIGSEGV).  g_activeRegistered holds exactly this root's
    // live registrations; entries can repeat, so dedupe while walking.
    QSet<const Layout::ResolvedWidget *> seen;
    for (const Layout::ResolvedWidget *w : g_activeRegistered) {
        if (!w || seen.contains(w)) continue;
        seen.insert(w);
        void *so = g_scriptObjByWidget.value(w);
        if (!so) continue;
        if (w->tag != QStringLiteral("text") &&
            w->tag != QStringLiteral("songticker")) continue;
        const QString t = w->attrs.value(QStringLiteral("text"));
        if (t.isEmpty()) continue;
        const std::wstring wt = t.toStdWString();
        const int n = Maki::fireOnTextChangedOnObject(so, wt.c_str());
        if (qEnvironmentVariableIntValue("WASABIQT_TRACE_META") == 1)
            std::fprintf(stderr, "[staticTextChanged] id=%s text='%s' so=%p fired=%d\n",
                w->id.toLocal8Bit().constData(), t.toLocal8Bit().constData(), so, n);
        if (n) ++fired;
    }
    return fired;
}

// Faithful per-GuiObject onResize cascade.  The Wasabi convention fires
// onResize on EACH GuiObject against its OWN script object + its OWN client
// rect.  Firing once on the layout root with the whole window rect (as a
// single broadcast does) is not enough: non-root onResize handlers (pledit
// g_playlist toolbar show/hide, footer/info-line reflow) never run and
// root-bound ones see the wrong size.  Walk every script-bound widget and
// fire onResize(0,0,w,h) with the widget's resolved client size; skip
// `skipObj` (the layout root, which the caller broadcasts separately) to
// avoid double-firing.  General, receiver-gated (only widgets that bound
// onResize pay).  Set WASABIQT_NO_PER_OBJECT_RESIZE=1 to disable.
int firePerObjectResize(void *skipObj) {
    if (::getenv("WASABIQT_NO_PER_OBJECT_RESIZE")) return 0;
    int fired = 0;
    // Per-root scoping: fire only on the ACTIVE root's script-bound
    // widgets.  Iterating the global map fired onResize on EVERY window's
    // objects — resizing the Playlist Editor visibly re-flowed the
    // player's titlebar text and drawer contents (and vice versa).  Real
    // Wasabi resizes are per-window; sibling windows stay untouched.
    for (const Layout::ResolvedWidget *w : g_activeRegistered) {
        void *so = g_scriptObjByWidget.value(w);
        if (!w || !so || so == skipObj) continue;
        const QRect r = w->lastCanvasRect;
        if (r.width() <= 0 || r.height() <= 0) continue;
        if (Maki::fireFourIntEventOnObject(so, L"onResize",
                                           0, 0, r.width(), r.height())) {
            ++fired;
            if (qEnvironmentVariableIntValue("WASABIQT_TRACE_MAKI") == 1)
                std::fprintf(stderr, "[perObjectResize] root=%p id=%s %dx%d\n",
                             g_activeRoot, w->id.toLocal8Bit().constData(),
                             r.width(), r.height());
        }
    }
    if (qEnvironmentVariableIntValue("WASABIQT_TRACE_MAKI") == 1)
        std::fprintf(stderr, "[perObjectResize] fired onResize on %d widgets\n", fired);
    return fired;
}

void clearWidgetRegistry() {
    g_byId.clear();
    g_layoutRootScriptObject = nullptr;
    // Drop the widget→scriptObject map for THIS root only (its registered
    // widget list) rather than .clear()-ing globally — otherwise reloading
    // one window (or opening a subwindow) would wipe another window's live
    // entries.  Pointer keys only (no deref), so dropping a reloading
    // root's freed widgets here is safe (the skin-switch UAF guard: the
    // old tree's Widgets are about to be freed, but fireStaticTextChanged
    // iterates g_scriptObjByWidget and would deref a dangling key —
    // removing them now prevents that, same as the old global clear did
    // for the single-window case).
    for (const Layout::ResolvedWidget *w : g_activeRegistered)
        g_scriptObjByWidget.remove(w);
    g_activeRegistered.clear();
    // g_scriptOwner is sid-keyed (shared across windows); its per-sid
    // teardown happens in SkinRuntime::Impl::destroyAll for exactly this
    // runtime's script ids, so it is NOT cleared globally here.
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

void setBitmapRegistry(BitmapRegistry *reg) {
    g_bitmapRegistry = reg;
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

// Trigger an immediate repaint from anywhere in the engine — fires
// the SkinView-registered callback (Widget::requestRepaint routes
// through this).  No-op when no callback is set.
void fireRepaint() {
    if (g_repaint) g_repaint();
}

// Named-window control (Maki System.showWindow / hideNamedWindow /
// isNamedWindowVisible).  The embedder registers a callback that maps a
// window reference (a GUID like "{0000000A-…}" or a container id) to its
// subwindow machinery.  op: 0 = hide, 1 = show, 2 = query visibility.
// Returns the window's visibility after the operation (0/1), or 0 when
// no callback is registered.  Process-global (not per-root): windows are
// app-level objects, and the embedder resolves the reference itself.
static std::function<int(const QString &, int)> g_namedWindowCb;
void registerNamedWindowCallback(std::function<int(const QString &, int)> cb) {
    g_namedWindowCb = std::move(cb);
}

// SkinView calls this so Maki getStatus() reads through to the host.
void registerSkinPlaybackStatusCallback(std::function<int()> cb) {
    g_playbackStatus = std::move(cb);
}

void registerSkinResizeCallback(std::function<void(int, int)> cb) {
    g_skinResize = std::move(cb);
}

// Embedder-supplied "perform this widget's click action" handler — drives
// GuiObject.leftClick()/rightClick() through the same action dispatch a real
// mouse click uses (see registerSkinWidgetClickCallback in SkinRuntime.h).
std::function<bool(const QString &, bool)> g_widgetClick;
void registerSkinWidgetClickCallback(
        std::function<bool(const QString &, bool)> cb) {
    g_widgetClick = std::move(cb);
}
bool triggerWidgetClick(const QString &widgetId, bool right) {
    return g_widgetClick ? g_widgetClick(widgetId, right) : false;
}

// Embedder-supplied graphic-equalizer accessors — drive Maki
// System.setEqBand()/getEqBand() (see registerSkinEqCallbacks in
// SkinRuntime.h).
std::function<void(int, int)> g_eqSetBand;
std::function<int(int)>       g_eqGetBand;
void registerSkinEqCallbacks(std::function<void(int, int)> setBand,
                             std::function<int(int)> getBand) {
    g_eqSetBand = std::move(setBand);
    g_eqGetBand = std::move(getBand);
}

// Maki Slider.setPosition/getPosition + System.setVolume/getVolume bridges
// (see registerSkinSliderCallbacks / registerSkinVolumeCallbacks).
std::function<void(const QString &, const QString &, int)> g_sliderSet;
std::function<int(const QString &, const QString &)>       g_sliderGet;
std::function<void(int)> g_volSet;
std::function<int()>     g_volGet;
void registerSkinSliderCallbacks(
        std::function<void(const QString &, const QString &, int)> setPos,
        std::function<int(const QString &, const QString &)> getPos) {
    g_sliderSet = std::move(setPos);
    g_sliderGet = std::move(getPos);
}
void registerSkinVolumeCallbacks(std::function<void(int)> setVol,
                                 std::function<int()> getVol) {
    g_volSet = std::move(setVol);
    g_volGet = std::move(getVol);
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
    void *opaque = qtWasabi::Maki::opaqueOf(it->scriptObject);
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

}  // namespace qtWasabi

// ── Bridge accessors used by wasabi-port/maki-bindings.cpp ──────

extern "C" {

void *wq_widget_findById(const wchar_t *id) {
    if (!id) return nullptr;
    auto it = qtWasabi::g_byId.constFind(qtWasabi::fromWide(id).toLower());
    return it == qtWasabi::g_byId.constEnd() ? nullptr : it->scriptObject;
}

// Maki named-window control — see registerNamedWindowCallback.
int wq_named_window(const wchar_t *ref, int op) {
    if (!ref || !qtWasabi::g_namedWindowCb) return 0;
    return qtWasabi::g_namedWindowCb(qtWasabi::fromWide(ref), op);
}

// Maki Timer backing (called by maki-bindings wq_setDelay/start/stop).
// Arm = create-or-restart a QTimer keyed by the Timer ScriptObject;
// on each tick fire <timer>.onTimer in the VM + repaint.  Runs on the
// GUI thread (Maki dispatch + the Qt event loop both live there).
void wq_timer_kill(void *timerSO);           // defined below; used by the
                                             // zombie-timer guard in the tick
void wq_timer_arm(void *timerSO, int ms) {   // low-level Maki Timer start
    if (!timerSO) return;
    if (ms < 1) ms = 1;
    QTimer *t = qtWasabi::g_makiTimers.value(timerSO, nullptr);
    if (!t) {
        t = new QTimer();
        QObject::connect(t, &QTimer::timeout, [timerSO]() {
            // Fire <timer>.onTimer in the VM.  Do NOT force a repaint
            // here: a Maki Timer can run at 1ms (Bento's dc_* poppler
            // timers are setDelay(1)), and an unconditional g_repaint per
            // tick rebuilds the whole window region ~1000x/sec, saturating
            // the GUI thread (dead clicks / no hover / slow dialog).  Any
            // visible change inside onTimer already repaints through the
            // setXmlParam / setData / setPosition / requestRepaint paths,
            // so a tick that changes nothing correctly costs nothing.
            if (qEnvironmentVariableIntValue("WASABIQT_TRACE_TIMER") == 1) {
                static int n = 0;
                if ((++n % 200) == 0)
                    std::fprintf(stderr, "[maki-timer] %d fires\n", n);
            }
            // Maki Timer fires onTimer only while `started`.
            if (!qtWasabi::g_timerStarted.count(timerSO)) return;
            // A timer whose owning window is GONE (skin switch dropped the
            // root; destroyAll has no per-timer teardown) must never fire —
            // its script objects are freed.  Kill the zombie.
            const void *own = qtWasabi::g_timerRoot.value(timerSO, nullptr);
            if (own && own != qtWasabi::activeScriptRoot() &&
                !qtWasabi::scriptRootAlive(own)) {
                wq_timer_kill(timerSO);
                return;
            }
            // Re-enter the VM under the timer's OWNING root (see
            // g_timerRoot).  Unknown owner → keep the current root.
            qtWasabi::ScopedScriptRoot guard(
                own ? own : qtWasabi::activeScriptRoot());
            qtWasabi::Maki::fireZeroArgEventOnObject(timerSO, L"onTimer");
        });
        qtWasabi::g_makiTimers.insert(timerSO, t);
    }
    // (Re)record the owner on EVERY arm: arms run under the owning root
    // (script execution is root-bracketed), and a heap-recycled Timer
    // pointer from a previous skin must not inherit the stale owner.
    qtWasabi::g_timerRoot.insert(timerSO, qtWasabi::activeScriptRoot());
    t->setInterval(ms);
    t->start();
    qtWasabi::g_timerStarted.insert(timerSO);
}
// Maki Timer setDelay — record-only; re-arm ONLY if already started.
void wq_timer_setDelay(void *timerSO, int ms) {
    if (qtWasabi::g_timerStarted.count(timerSO)) wq_timer_arm(timerSO, ms);
}
// Maki Timer stop — clear started + halt, but KEEP the QTimer for a later start().
void wq_timer_stop(void *timerSO) {
    QTimer *t = qtWasabi::g_makiTimers.value(timerSO, nullptr);
    if (t) t->stop();
    qtWasabi::g_timerStarted.erase(timerSO);
}
// Maki Timer isRunning — the started flag, not the QTimer's active state.
int wq_timer_isRunning(void *timerSO) {
    return qtWasabi::g_timerStarted.count(timerSO) ? 1 : 0;
}
// Object teardown (delete <timer>) — destroy the QTimer entirely.
void wq_timer_kill(void *timerSO) {
    QTimer *t = qtWasabi::g_makiTimers.take(timerSO);
    if (t) { t->stop(); t->deleteLater(); }
    qtWasabi::g_timerStarted.erase(timerSO);
    qtWasabi::g_timerRoot.remove(timerSO);
}

// Scoped findObject: search the RECEIVER widget's subtree first, so a
// reused id (e.g. each InfoLine's child <Text id="text">) resolves to
// THIS group's own child rather than whichever one last won the flat
// g_byId map.  Matches Wasabi findObject() semantics (descendant
// search from the callee).  Falls back to the global lookup when the
// receiver is null or has no such descendant.
void *wq_widget_findByIdScoped(void *parentHandle, const wchar_t *id) {
    if (!id) return nullptr;
    const QString want = qtWasabi::fromWide(id).toLower();
    const bool trc = (want == QStringLiteral("label") || want == QStringLiteral("text")) &&
                     qEnvironmentVariableIntValue("WASABIQT_TRACE_META") == 1;
    if (parentHandle) {
        void *opaque = qtWasabi::Maki::opaqueOf(parentHandle);
        auto *parent =
            static_cast<qtWasabi::Layout::ResolvedWidget *>(opaque);
        if (trc) std::fprintf(stderr, "[fbi] id=%ls parent=%p opaque=%p parentId=%s\n",
            id, parentHandle, opaque,
            parent ? parent->id.toLocal8Bit().constData() : "(null)");
        if (parent) {
            std::function<qtWasabi::Layout::ResolvedWidget *(
                qtWasabi::Layout::ResolvedWidget &)> dfs =
                [&](qtWasabi::Layout::ResolvedWidget &w)
                    -> qtWasabi::Layout::ResolvedWidget * {
                    for (auto &c : w.children) {
                        if (!c) continue;
                        if (c->attrs.value(QStringLiteral("id")).toLower()
                                == want ||
                            c->instanceId.toLower() == want)
                            return c.get();
                        if (auto *r = dfs(*c)) return r;
                    }
                    return nullptr;
                };
            if (auto *match = dfs(*parent)) {
                void *so = qtWasabi::g_scriptObjByWidget.value(match, nullptr);
                if (!so) {
                    // The matched descendant has no script object yet
                    // (the engine only makes them for script-owning
                    // widgets).  Create one bound to THIS widget +
                    // cache it, so findObject hands back a DISTINCT
                    // handle per instance — without this, every
                    // InfoLine's value child fell back to the one
                    // global "text" widget and all setText collided.
                    so = qtWasabi::Maki::createWidgetScriptObject(match);
                    qtWasabi::g_scriptObjByWidget.insert(match, so);
                    // Track for per-root teardown: clearWidgetRegistry drops
                    // only g_activeRegistered keys, so an untracked on-demand
                    // entry would outlive its widget — fireStaticTextChanged
                    // iterates this map and derefs the keys (the reload-skin
                    // use-after-free crash).
                    qtWasabi::g_activeRegistered.append(match);
                }
                if (trc)
                    std::fprintf(stderr, "[fbi]   -> MATCH match=%p so=%p\n",
                                 (void *)match, so);
                return so;
            }
            if (trc) std::fprintf(stderr, "[fbi]   -> no descendant match (fallback global)\n");
        }
    } else if (trc) {
        std::fprintf(stderr, "[fbi] id=%ls parent=NULL (fallback global)\n", id);
    }
    return wq_widget_findById(id);
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
    if (handle == qtWasabi::g_layoutRootScriptObject &&
        std::wcscmp(name, L"visible") == 0 &&
        value && std::wcscmp(value, L"0") == 0) {
        if (std::getenv("WASABIQT_TRACE_LAYOUTROOT"))
            std::fprintf(stderr,
                "[wq_widget_setAttr] refusing to hide active layout root\n");
        return;
    }
    // The handle is a WidgetScriptObject*.  We get its opaque widget
    // ptr via the public bridge (avoids re-including the Maki
    // ScriptObject header here).
    //
    // Routes through Widget::setXmlParam (virtual) so subclasses can
    // capture changes to typed state members alongside the attrs
    // hash — see ComponentBucketWidget::setXmlParam for the
    // canonical example (`_scroll` / `_entry_step` shadowed onto
    // typed ints for paint + hit-test).
    void *opaque = qtWasabi::Maki::opaqueOf(handle);
    auto *w = static_cast<qtWasabi::Widget *>(opaque);
    if (!w) return;

    const QString newVal = qtWasabi::fromWide(value);
    // Settle: flag a geometry change only when the value actually moves,
    // so dispatchInitialResize's fixpoint loop converges (a handler that
    // re-writes the SAME value every onResize must not spin it forever).
    if (qtWasabi::isGeometryAttr(name) &&
        w->attrs.value(qtWasabi::fromWide(name)) != newVal)
        qtWasabi::g_geometryDirty = true;
    if (std::getenv("WASABIQT_TRACE_GEOSET") && qtWasabi::isGeometryAttr(name))
        std::fprintf(stderr, "[geoset] root=%p id=%s %ls=%ls (was %s)\n",
                     qtWasabi::activeScriptRoot(),
                     w->id.toLocal8Bit().constData(),
                     name, value ? value : L"",
                     w->attrs.value(qtWasabi::fromWide(name))
                         .toLocal8Bit().constData());
    if (std::getenv("WASABIQT_TRACE_PLENLARGE") && !w->id.isEmpty()) {
        const QString idl = w->id;
        if (idl.contains("mainframe") || idl.contains("dualwnd") ||
            idl == "sui.content" || idl.startsWith("player.component.playlist") ||
            idl.startsWith("player.playlist.") || idl.startsWith("wdh.") ||
            idl.startsWith("PlaylistPro"))
            std::fprintf(stderr, "[plenlarge] setXmlParam(%s, %ls = %ls)\n",
                         idl.toLocal8Bit().constData(), name, value ? value : L"");
    }
    w->setXmlParam(qtWasabi::fromWide(name), newVal);
    if (qtWasabi::g_repaint) qtWasabi::g_repaint();
}

const wchar_t *wq_widget_getAttr(void *handle, const wchar_t *name) {
    static thread_local QString cache;     // keep alive for the call
    if (!handle || !name) return L"";
    void *opaque = qtWasabi::Maki::opaqueOf(handle);
    auto *w = static_cast<qtWasabi::Layout::ResolvedWidget *>(opaque);
    if (!w) return L"";
    cache = w->attrs.value(qtWasabi::fromWide(name));
    static thread_local std::wstring buf;
    buf.assign(cache.toStdWString());
    return buf.c_str();
}

// Maki GuiObject.leftClick()/rightClick() action fallback: perform the
// widget's `action=` through the embedder's real-click dispatch.  Called by
// wq_leftClick/wq_rightClick after the onLeftClick/onRightClick script
// callback didn't consume the click.  Returns 1 if handled.
int wq_widget_click_action(void *handle, int right) {
    if (!handle) return 0;
    const wchar_t *id = wq_widget_getAttr(handle, L"id");
    if (!id || !*id) return 0;
    return qtWasabi::triggerWidgetClick(QString::fromWCharArray(id),
                                        right != 0) ? 1 : 0;
}

// Maki System.setEqBand/getEqBand → embedder EQ store (see
// registerSkinEqCallbacks).
void wq_eq_set_band(int band, int val) {
    if (qtWasabi::g_eqSetBand) qtWasabi::g_eqSetBand(band, val);
}
int wq_eq_get_band(int band) {
    return qtWasabi::g_eqGetBand ? qtWasabi::g_eqGetBand(band) : 0;
}

// Maki Slider.setPosition/getPosition → embedder host slider axis, keyed by
// the slider node's own action=/param= attrs (so no per-skin/per-action code).
void wq_slider_set_position(void *handle, int value255) {
    if (!handle || !qtWasabi::g_sliderSet) return;
    void *opaque = qtWasabi::Maki::opaqueOf(handle);
    auto *w = static_cast<qtWasabi::Layout::ResolvedWidget *>(opaque);
    if (!w) return;
    qtWasabi::g_sliderSet(w->attrs.value(QStringLiteral("action")),
                          w->attrs.value(QStringLiteral("param")), value255);
}
int wq_slider_get_position(void *handle) {
    if (!handle || !qtWasabi::g_sliderGet) return -1;
    void *opaque = qtWasabi::Maki::opaqueOf(handle);
    auto *w = static_cast<qtWasabi::Layout::ResolvedWidget *>(opaque);
    if (!w) return -1;
    return qtWasabi::g_sliderGet(w->attrs.value(QStringLiteral("action")),
                                 w->attrs.value(QStringLiteral("param")));
}
void wq_volume_set(int v255) { if (qtWasabi::g_volSet) qtWasabi::g_volSet(v255); }
int  wq_volume_get() { return qtWasabi::g_volGet ? qtWasabi::g_volGet() : 0; }

// Return the script-object handle of `handle`'s parent group in the
// resolved tree (or null if it has no parent / isn't resolved).  Backs the
// Maki getParent() binding.  parentWidget is maintained by
// Widget::cacheResolvedRects every layout pass; we lazily create + cache a
// script object for the parent the same way findObject does.
void *wq_widget_parent(void *handle) {
    if (!handle) return nullptr;
    void *opaque = qtWasabi::Maki::opaqueOf(handle);
    auto *w = static_cast<qtWasabi::Layout::ResolvedWidget *>(opaque);
    if (!w || !w->parentWidget) return nullptr;
    qtWasabi::Layout::ResolvedWidget *p = w->parentWidget;
    void *so = qtWasabi::g_scriptObjByWidget.value(p, nullptr);
    if (!so) {
        so = qtWasabi::Maki::createWidgetScriptObject(p);
        qtWasabi::g_scriptObjByWidget.insert(p, so);
        qtWasabi::g_activeRegistered.append(p);   // per-root teardown
    }
    return so;
}

// Maki Frame.setPosition(pos): move the frame's live divider and re-split
// its panes (Layout::applyFrameDividerPos), then re-resolve the whole tree
// so the new geometry shows in paint + hit-test + getWidth, and repaint.
// This is what lets pledit.m drive the playlist enlarge/collapse itself
// (dualwnd.setPosition(w)/setPosition(0)) instead of the static C++ pass.
void wq_widget_setFrameDivider(void *handle, int pos) {
    if (!handle) return;
    void *opaque = qtWasabi::Maki::opaqueOf(handle);
    auto *node = static_cast<qtWasabi::Layout::ResolvedWidget *>(opaque);
    if (!node ||
        !node->attrs.contains(QStringLiteral("_frame_divpos")))
        return;   // not a Wasabi:Frame node — ignore, per Wasabi semantics
    // Settle: a divider move reflows the panes, so flag it for the
    // onResize fixpoint — but only when the position actually changes.
    if (node->attrs.value(QStringLiteral("_frame_divpos")).toInt() != pos)
        qtWasabi::g_geometryDirty = true;
    qtWasabi::Layout::applyFrameDividerPos(*node, pos);
    if (qtWasabi::g_layoutRootScriptObject) {
        void *ro = qtWasabi::Maki::opaqueOf(qtWasabi::g_layoutRootScriptObject);
        auto *root = static_cast<qtWasabi::Layout::ResolvedWidget *>(ro);
        if (root) {
            const QSize canvas = root->lastCanvasRect.size();
            if (canvas.isValid() && !canvas.isEmpty())
                root->cacheResolvedRects(QPoint(0, 0), canvas);
        }
    }
    if (qtWasabi::g_repaint) qtWasabi::g_repaint();
}

int wq_widget_getAttrInt(void *handle, const wchar_t *name) {
    if (!handle || !name) return 0;
    void *opaque = qtWasabi::Maki::opaqueOf(handle);
    auto *w = static_cast<qtWasabi::Layout::ResolvedWidget *>(opaque);
    if (!w) return 0;
    return w->attrs.value(qtWasabi::fromWide(name)).toInt();
}

// Effective resolved pixel width/height from the last cacheResolvedRects
// pass (run after layout, before script onResize).  Returns 0 if the
// widget was never resolved (caller falls back to the raw `w`/`h` attr
// or text-measure).  This is how relat-sized widgets (w="0" relatw="1")
// report their real pixel size to Maki getWidth()/getHeight().
int wq_widget_resolvedWidth(void *handle) {
    if (!handle) return 0;
    void *opaque = qtWasabi::Maki::opaqueOf(handle);
    auto *w = static_cast<qtWasabi::Layout::ResolvedWidget *>(opaque);
    if (!w) return 0;
    const QRect r = w->lastCanvasRect;
    return r.isValid() ? r.width() : 0;
}
int wq_widget_resolvedHeight(void *handle) {
    if (!handle) return 0;
    void *opaque = qtWasabi::Maki::opaqueOf(handle);
    auto *w = static_cast<qtWasabi::Layout::ResolvedWidget *>(opaque);
    if (!w) return 0;
    const QRect r = w->lastCanvasRect;
    return r.isValid() ? r.height() : 0;
}
// Resolved on-screen x/y (absolute canvas coords) from the last layout pass.
// Backs Maki getLeft()/getTop() (RESOLVED position), distinct from getGuiX/
// getGuiY (RAW declared attr).  hasResolved* lets the binding fall back to
// the raw attr when the widget was never resolved.
bool wq_widget_hasResolvedRect(void *handle) {
    if (!handle) return false;
    void *opaque = qtWasabi::Maki::opaqueOf(handle);
    auto *w = static_cast<qtWasabi::Layout::ResolvedWidget *>(opaque);
    return w && w->lastCanvasRect.isValid();
}
int wq_widget_resolvedX(void *handle) {
    if (!handle) return 0;
    void *opaque = qtWasabi::Maki::opaqueOf(handle);
    auto *w = static_cast<qtWasabi::Layout::ResolvedWidget *>(opaque);
    return (w && w->lastCanvasRect.isValid()) ? w->lastCanvasRect.x() : 0;
}
int wq_widget_resolvedY(void *handle) {
    if (!handle) return 0;
    void *opaque = qtWasabi::Maki::opaqueOf(handle);
    auto *w = static_cast<qtWasabi::Layout::ResolvedWidget *>(opaque);
    return (w && w->lastCanvasRect.isValid()) ? w->lastCanvasRect.y() : 0;
}
// Group child enumeration — backs Maki getNumObjects()/enumObject(i).  Were
// unbound → 0, so `for(i=0;i<grp.getNumObjects();i++) grp.enumObject(i)` did
// zero iterations (any per-child layout/init driven by enumeration silently
// no-op'd).  childAt returns the i-th child's script object (lazily created
// + cached, same as findObject).  General; latent for current skins.
int wq_widget_childCount(void *handle) {
    if (!handle) return 0;
    void *opaque = qtWasabi::Maki::opaqueOf(handle);
    auto *w = static_cast<qtWasabi::Layout::ResolvedWidget *>(opaque);
    if (!w) return 0;
    int n = 0;
    for (const auto &c : w->children) if (c) ++n;
    return n;
}
void *wq_widget_childAt(void *handle, int idx) {
    if (!handle || idx < 0) return nullptr;
    void *opaque = qtWasabi::Maki::opaqueOf(handle);
    auto *w = static_cast<qtWasabi::Layout::ResolvedWidget *>(opaque);
    if (!w) return nullptr;
    int n = 0;
    for (const auto &c : w->children) {
        if (!c) continue;
        if (n == idx) {
            qtWasabi::Layout::ResolvedWidget *child = c.get();
            void *so = qtWasabi::g_scriptObjByWidget.value(child, nullptr);
            if (!so) {
                so = qtWasabi::Maki::createWidgetScriptObject(child);
                qtWasabi::g_scriptObjByWidget.insert(child, so);
                qtWasabi::g_activeRegistered.append(child);  // per-root teardown
            }
            return so;
        }
        ++n;
    }
    return nullptr;
}

// Auto-width for <text>/<songticker> widgets.  Mirrors Wasabi's
// Text auto-width preference: per-segment text width + 4 (Wasabi
// convention) + 7 (Win32-GDI / Qt-QFontMetrics width difference at the
// matching pixel size for Arial Bold) = + 11.  Returns -1 to signal
// "not a text widget — caller should fall back to the declared `w`".
//
// The script-side `getAutoWidth` is what the titlebar uses to size
// the streak gap around the title; without this calculation the
// streaks misalign with the rendered text width.
int wq_widget_textWidth(void *handle) {
    if (!handle) return -1;
    void *opaque = qtWasabi::Maki::opaqueOf(handle);
    auto *w = static_cast<qtWasabi::Layout::ResolvedWidget *>(opaque);
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
    const bool bold = w->attrs.value(QStringLiteral("bold")) == QStringLiteral("1");
    if (ok && fontsize > 0)
        f.setPixelSize(qtWasabi::wasabiFontPixelSize(fontsize, family, bold));
    if (bold) f.setBold(true);

    // getAutoWidth maps to the Wasabi Text widget's SUGGESTED_W, which is
    // `getTextWidth + 4 + lpadding + rpadding` (the +4 is the per-segment
    // pad the engine adds around each tab-delimited run; a plain title is
    // one segment).  The streak/centre math in titlebar.maki relies on this
    // exact box width — the box is 4px wider than the glyph advance and the
    // text draws at box.left + 1, so the glyph sits 1px left of the box
    // centre, which is what gives the reference title its slight-left bias.
    // The pixel size is the same per-font mapping the renderer uses, so the
    // measured box and the painted glyphs agree.
    QFontMetrics fm(f);
    const int lpad = w->attrs.value(QStringLiteral("lpadding")).toInt();
    const int rpad = w->attrs.value(QStringLiteral("rpadding")).toInt();
    return fm.horizontalAdvance(s) + 4 + lpad + rpad;
}

// `<layer>` widgets' autoWidth/Height = the intrinsic pixel size of
// the bound `image="…"` bitmap.  The Wasabi Layer SUGGESTED_W/H
// preference returns the bitmap source-rect dimensions.  Without
// this, Bento's `mainmenu.maki` reads w=0 for every File/Play/
// Options/View/Help layer (the layers declare no explicit `w` attr)
// → xpos stays 0 → all five menu items pile up at x=0 instead of
// laying out across the titlebar.
int wq_widget_bitmapWidth(void *handle) {
    if (!handle || !qtWasabi::g_bitmapRegistry) return -1;
    void *opaque = qtWasabi::Maki::opaqueOf(handle);
    auto *w = static_cast<qtWasabi::Layout::ResolvedWidget *>(opaque);
    if (!w) return -1;
    const QString img = w->attrs.value(QStringLiteral("image"));
    if (img.isEmpty()) return -1;
    const QImage bm = qtWasabi::g_bitmapRegistry->imageFor(img);
    if (bm.isNull()) return -1;
    return bm.width();
}
int wq_widget_bitmapHeight(void *handle) {
    if (!handle || !qtWasabi::g_bitmapRegistry) return -1;
    void *opaque = qtWasabi::Maki::opaqueOf(handle);
    auto *w = static_cast<qtWasabi::Layout::ResolvedWidget *>(opaque);
    if (!w) return -1;
    const QString img = w->attrs.value(QStringLiteral("image"));
    if (img.isEmpty()) return -1;
    const QImage bm = qtWasabi::g_bitmapRegistry->imageFor(img);
    if (bm.isNull()) return -1;
    return bm.height();
}

// Hand back the synthetic main-layout handle.  Returns nullptr if
// no skin has been loaded yet, in which case the caller falls back
// to the script's own widget (preserving the previous behaviour).
void *wq_layout_root() {
    return qtWasabi::g_layoutRootScriptObject;
}

// Lookup the owner widget for `sid`.  Returns nullptr if no owner
// was registered (e.g. for top-level scripts), in which case the
// caller falls back to the layout root or the receiver.
void *wq_script_owner(int sid) {
    return qtWasabi::scriptOwnerScriptObject(sid);
}

// Host playback status (1 playing, -1 paused, 0 stopped) — used by
// Maki's getStatus().  Returns 0 when no callback has been registered.
int wq_playback_status() {
    return qtWasabi::g_playbackStatus ? qtWasabi::g_playbackStatus() : 0;
}

void wq_layout_set_target_w(int w) { qtWasabi::g_targetW = w; }
void wq_layout_set_target_h(int h) { qtWasabi::g_targetH = h; }
void wq_layout_set_target_x(int x) { qtWasabi::g_targetX = x; }
void wq_layout_set_target_y(int y) { qtWasabi::g_targetY = y; }

// Desktop work-area geometry — backs System.getViewport*/getViewPort*From-
// GuiObject.  Maki maximize scripts (e.g. simplemaximize.maki) compare the
// layout size against these to choose maximize-vs-restore and resize the
// layout to them on maximize.  Returns 0 when there is no screen (offscreen
// edge case) so the binding can fall back.  GENERAL: any skin's maximize.
int wq_screen_avail_w() {
    auto *s = QGuiApplication::primaryScreen();
    return s ? s->availableGeometry().width() : 0;
}
int wq_screen_avail_h() {
    auto *s = QGuiApplication::primaryScreen();
    return s ? s->availableGeometry().height() : 0;
}
int wq_screen_avail_x() {
    auto *s = QGuiApplication::primaryScreen();
    return s ? s->availableGeometry().x() : 0;
}
int wq_screen_avail_y() {
    auto *s = QGuiApplication::primaryScreen();
    return s ? s->availableGeometry().y() : 0;
}

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

// Read-side accessor exposed in the public qtWasabi namespace (its
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
    void *opaque = qtWasabi::Maki::opaqueOf(handle);
    auto *w = static_cast<qtWasabi::Layout::ResolvedWidget *>(opaque);
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
        qtWasabi::Maki::fireZeroArgEventOnObject(handle, L"onTargetReached");
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
            if (qtWasabi::g_repaint) qtWasabi::g_repaint();
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
            qtWasabi::Maki::fireZeroArgEventOnObject(handle, L"onTargetReached");
        });
    m[handle] = anim;
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

// gotoTarget — apply the stashed setTarget* values.  This path does not
// tween (the Wasabi setTargetSpeed timing is ignored here); the embedder
// gets the final size immediately.  Both w and h fall back to the current
// layout root w/h when un-set so a script that only changes one dimension
// doesn't accidentally collapse the other.
void wq_layout_goto_target() {
    if (!qtWasabi::g_skinResize) {
        qtWasabi::g_targetW = qtWasabi::g_targetH = -1;
        qtWasabi::g_targetX = qtWasabi::g_targetY = -1;
        return;
    }
    int w = qtWasabi::g_targetW;
    int h = qtWasabi::g_targetH;
    // Fall back to the layout root's current attrs when un-set.
    if (w < 0 || h < 0) {
        if (auto *root = static_cast<qtWasabi::Layout::ResolvedWidget *>(
                qtWasabi::Maki::opaqueOf(qtWasabi::g_layoutRootScriptObject))) {
            if (w < 0) w = root->attrs.value(QStringLiteral("w")).toInt();
            if (h < 0) h = root->attrs.value(QStringLiteral("h")).toInt();
        }
    }
    if (::getenv("WASABIQT_TRACE_MAKI"))
        ::fprintf(stderr, "[maki] gotoTarget w=%d h=%d (raw target=%d,%d)\n",
                  w, h, qtWasabi::g_targetW, qtWasabi::g_targetH);
    if (w > 0 && h > 0) qtWasabi::g_skinResize(w, h);
    qtWasabi::g_targetW = qtWasabi::g_targetH = -1;
    qtWasabi::g_targetX = qtWasabi::g_targetY = -1;
    // Fire onTargetReached only if the embedder's resize callback
    // didn't claim it'd fire it later (the animated path sets this
    // flag while it's still tweening, then clears + fires on tick
    // completion via wq_fire_target_reached).
    if (qtWasabi::g_animatedResizePending) return;

    // Synthesise the animation-complete event the script expects.
    // drawer.m's __main.onTargetReached() reads __drawer_direction
    // and fires onDoneOpeningDrawer / onDoneClosingDrawer — the close
    // chain in turn hides AVSGroup so the chrome doesn't render
    // through it once the window has shrunk back.  Wasabi fires this
    // when its resize animation completes; the snap path here fires
    // immediately.
    if (qtWasabi::g_layoutRootScriptObject) {
        qtWasabi::Maki::fireZeroArgEventOnObject(
            qtWasabi::g_layoutRootScriptObject, L"onTargetReached");
    }
}

// Embedder-driven async-resize completion.  An animated resize
// callback sets `g_animatedResizePending = true` synchronously when
// it starts the tween, suppressing the bridge's auto-fired
// onTargetReached.  When the tween finishes, the embedder calls this
// to deliver the event the scripts expect.
void wq_fire_target_reached() {
    if (!qtWasabi::g_layoutRootScriptObject) return;
    qtWasabi::g_animatedResizePending = false;
    qtWasabi::Maki::fireZeroArgEventOnObject(
        qtWasabi::g_layoutRootScriptObject, L"onTargetReached");
}

}  // extern "C"

namespace qtWasabi {
int widgetAnimationsActive() { return ::g_widgetAnimsActive; }
}  // namespace qtWasabi
