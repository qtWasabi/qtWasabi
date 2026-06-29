#pragma once
//
// SkinRuntime — owns Maki script execution against a resolved widget
// tree.  Each widget gets a `WidgetScriptObject*` (created via
// maki-bridge); each `<script file=…/>` reference in the skin XML gets
// loaded into the Maki VCPU; per-script `param=` tokens drive the
// variable→widget binding.

#include <QtCore/qglobal.h>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

namespace qtWasabi {

namespace SkinXml { struct Document; }
class Widget;
namespace Layout  { using ResolvedWidget = ::qtWasabi::Widget; }

class SkinRuntime {
public:
    SkinRuntime();
    ~SkinRuntime();

    // Walk `root`, create a WidgetScriptObject for every widget that
    // has an id, then load every `<script file=…/>` referenced in
    // `doc`.  Bindings run on first call and on every reload.
    int loadScripts(const SkinXml::Document &doc,
                    Layout::ResolvedWidget &root);

    // Provide the bitmap registry so `<layer>` widgets' getAutoWidth
    // / getAutoHeight can return the bound bitmap's intrinsic size.
    // Bento's mainmenu.maki uses these to lay out the File/Play/
    // Options/View/Help menu items sequentially across the titlebar.
    // Call before dispatchOnScriptLoaded.
    void setBitmapRegistry(class BitmapRegistry *reg);

    // Fire the System.onScriptLoaded handler for every loaded script.
    // Returns the number of scripts where dispatch actually started
    // (i.e. their DLF table contains an "onScriptLoaded" entry AND
    // the bound SystemObject has a matching event entry).
    //
    // BIG WARNING: this WILL hit unimplemented stubs partway
    // through real script execution.  Several SOM / GuiObject / Group
    // method bodies still return defaults, and the opensourced VM hits
    // ASSERTs (or worse) once those defaults violate an invariant.
    // Run with WASABIQT_FATAL_ASSERTS=0 (the default) to log rather
    // than abort.  Stable use needs real method bodies for
    // setXmlParam / findObject / getAutoWidth / etc.
    int dispatchOnScriptLoaded();

    // Register the embedder's play-item metadata resolver so the
    // skin's file-info scripts (fileinfo.maki) can read real track
    // metadata.  `key` is lower-case: "playitem:string" (filename),
    // "playitem:displaytitle", "decoder", "meta:<field>"
    // (title/artist/album/year/genre/albumartist/composer/...).
    // Return an empty QString for unknown/no-track.
    void setPlayItemMetadataResolver(
        std::function<QString(const QString &key)> resolver);

    // Fire System.onTitleChange(title) on every loaded script — call
    // when the current track (or its metadata) changes so fileinfo.maki
    // repopulates + shows the Title/Artist/Album/… lines.  Returns the
    // number of scripts that handled it.
    int dispatchTitleChange(const QString &title);

    // Playback transport state — maps to the Maki System playback
    // callbacks below.  Resumed is distinct from Playing so a skin can
    // tell a fresh start (onPlay) from un-pausing (onResume).
    enum class PlaybackState { Playing, Resumed, Paused, Stopped };

    // Fire the matching System playback callback (onPlay / onResume /
    // onPause / onStop) on every loaded script — call when the host's
    // transport state changes so a skin can swap its play/pause chrome
    // (e.g. HeadAMP's overlaid Play/Pause buttons).  Returns the number
    // of scripts that handled it.
    int dispatchPlaybackState(PlaybackState state);

    // After onScriptLoaded, fire System.onSetXuiParam(name, value)
    // for every non-standard attribute on a frame instantiation
    // (e.g. <Wasabi:MainFrame:NoStatus padtitleleft="10"/>).  Real
    // Wasabi delivers these to the embedded group's scripts at
    // instantiation; without them, titlebar.m's resizeObjects
    // never runs.  Broadcasts to every loaded script — scripts
    // that don't have an onSetXuiParam handler ignore the event.
    int dispatchXuiParams(const Layout::ResolvedWidget &root);

    // Fire main.onResize(0, 0, layoutW, layoutH) on the layout root
    // for every loaded script.  Real Wasabi fires this whenever the
    // layout is resized; at load time we issue an initial event so
    // scripts that re-flow content based on the layout's actual size
    // (configtabs.m centring DrawerContent, mainmenu.m clamping
    // overlay widths) run without waiting for a real resize.  Returns
    // the number of scripts whose handler fired.
    int dispatchInitialResize(int layoutW, int layoutH);


    // Number of scripts the VM currently holds.  Includes scripts
    // that loaded but had errors during onScriptLoaded.
    int scriptCount() const;

    // Number of widget objects bound — should be ≥ unique-id count
    // in the resolved tree.
    int widgetObjectCount() const;

    // Reset the VM to an empty state.  Discards all scripts +
    // widget objects (handles are deleted).
    void reset();

private:
    Q_DISABLE_COPY(SkinRuntime)
    struct Impl;
    Impl *m_d;
};

// Register a callback the runtime fires whenever a script
// mutates a widget attribute. SkinView wires this up in its ctor so
// scripts that change padleft/padright/etc. trigger a repaint.
// Pass an empty std::function to unregister.
void registerSkinRepaintCallback(std::function<void()> cb);

// Register a callback the Maki getStatus() binding queries on each
// dispatch.  Return values follow the Wasabi convention:
//   1 = playing, -1 = paused, 0 = stopped.
// Pass an empty std::function to unregister (getStatus() then
// returns 0).
void registerSkinPlaybackStatusCallback(std::function<int()> cb);

// Register the named-window callback for Maki System.showWindow /
// hideNamedWindow / isNamedWindowVisible.  The reference is a window
// GUID ("{0000000A-…}") or container id; op: 0 = hide, 1 = show,
// 2 = query.  Return the window's visibility (0/1) after the operation.
// The embedder routes this to its subwindow machinery (the same path
// the TOGGLE action uses).  Pass an empty std::function to unregister.
void registerNamedWindowCallback(std::function<int(const QString &, int)> cb);

// Register a callback Maki Layout.setTarget* + gotoTarget invokes
// when scripts resize the layout (e.g. videoavs.m opening the
// upper drawer calls main.setTargetH(taller); main.gotoTarget()).
// The callback gets the requested (width, height); embedders typically
// resize the SkinView widget and let the chrome reflow.  Pass an
// empty std::function to unregister.
//
// SYNCHRONOUS path (default): the callback resizes immediately and
// the bridge fires `onTargetReached` on the layout root right after.
// Suits a snap-resize implementation.
//
// ASYNC / ANIMATED path: the callback starts a tween and returns
// immediately; the embedder MUST call `beginAnimatedResize()` before
// returning so the bridge suppresses its synchronous
// `onTargetReached`, then call `fireTargetReached()` when the tween
// finishes so scripts run their onDoneOpening/Closing handlers at the
// real animation-complete instant.
void registerSkinResizeCallback(std::function<void(int w, int h)> cb);

// Set the "animated resize is in flight" flag — the resize callback
// calls this synchronously when it kicks off a tween so the bridge
// doesn't fire onTargetReached right after the callback returns.
void beginAnimatedResize();

// Fire `onAction(action, param, x, y, p1, p2, source)` on the
// widget identified by `targetId` — used when a button with
// `action_target="X"` is clicked: configtarget.m and friends pivot
// on this event to swap pages, change visualisation, etc.
//
// `sourceId` names the source widget (the clicked button); pass
// an empty string when there isn't one.  Returns the number of
// handlers fired across all loaded scripts.
int fireWidgetActionEvent(const QString &targetId,
                          const QString &action,
                          const QString &param,
                          int x, int y, int p1, int p2,
                          const QString &sourceId);

// Directly mutate a widget's attribute by id — equivalent to
// Maki's `setXmlParam(name, value)` from the script side, but
// callable from C++ embedders.  Triggers the repaint callback so
// the chrome reflects the change.  Returns true if the widget was
// found.
bool fireWidgetAttrSet(const QString &widgetId,
                       const QString &name,
                       const QString &value);

// Number of widget-level setTarget animations currently running
// (drawer slide, sub-widget tweens).  Embedders can suspend
// auto-fitting / window-resize chatter while > 0 — Wayland
// surface reconfigures per frame are expensive and visibly lag
// behind the animation, leaving the chrome clipped short of the
// target extent.
int widgetAnimationsActive();

// Fire onTargetReached on the layout root — embedders' animated
// resize handlers call this when the tween completes.  No-op if no
// layout root is registered (e.g. before SkinRuntime::loadScripts).
void fireTargetReached();

// Fire a Maki event by name against the widget at `widgetId`, looked
// up case-insensitively in the global SkinRuntimeBridge widget
// registry.  Used by embedders to dispatch input events (button
// clicks, hovers, …) to the per-widget event handlers any loaded
// script bound via `widget.onLeftClick() {}` syntax.  Returns the
// number of script handlers that fired.  Common event names:
// `onLeftClick`, `onLeftButtonDown`, `onRightClick`,
// `onMouseMove`, `onMouseEnter`, `onMouseLeave`.
int fireWidgetEvent(const QString &widgetId, const wchar_t *eventName);

// Simulate a real click on the widget identified by `widgetId`, performing
// its `action=` exactly as a mouse click would (builtin transport/window
// verbs, TOGGLE, action_target/onAction).  The embedder registers the
// handler; the engine calls it from Maki's GuiObject.leftClick()/rightClick()
// after the onLeftClick/onRightClick script callback didn't consume the click
// — so a script that delegates a click (`otherButton.leftClick()`) drives the
// other button's ACTION too, not just its script handler, on any skin.
// Returns true if the click was handled.
bool triggerWidgetClick(const QString &widgetId, bool right);
void registerSkinWidgetClickCallback(
    std::function<bool(const QString &widgetId, bool right)> cb);

// Graphic-equalizer bridge: Maki System.setEqBand(band,val)/getEqBand(band)
// route through these so a skin's EQ reset / +/- buttons and any script that
// reads or recalls band gains work on EVERY skin.  `band` is 0-9; `val` is
// Wasabi's signed gain (-127..127, 0 = flat).  The embedder maps it onto its
// own EQ store so the buttons and the EQ sliders stay in lockstep.
void registerSkinEqCallbacks(std::function<void(int band, int val)> setBand,
                             std::function<int(int band)> getBand);

// Slider bridge: Maki Slider.setPosition(value)/getPosition() route through
// these (the embedder maps the slider's `action=`/`param=` onto its own host
// axis).  `value` is the Winamp API slider value space, 0..255 (128 = centre
// for a balance/pan slider).  getPos returns -1 when the action has no host
// value.  This is what makes scripted balance/volume buttons (which call
// Slider.setPosition on the target slider) actually move the audio on any skin.
void registerSkinSliderCallbacks(
    std::function<void(const QString &action, const QString &param,
                       int value255)> setPos,
    std::function<int(const QString &action, const QString &param)> getPos);

// Volume bridge: Maki System.setVolume(v)/getVolume() route through these.
// `v` is the Winamp API 0..255 scale; the embedder maps onto its own store.
void registerSkinVolumeCallbacks(std::function<void(int v255)> setVol,
                                 std::function<int()> getVol);

// ── Per-window Maki dispatch scoping ───────────────────────────────
// The single Maki VM resolves widget ids / the layout-root pseudo /
// repaint+resize callbacks against the ACTIVE "script root" — mirroring
// real Wasabi resolving findObject relative to the dispatching window
// (getRootWnd()->findWindow).  `rootKey` is a window's layout-tree root
// pointer (`&SkinView::tree()` / the player's root Widget*).  The player
// is the permanent resting root; a subwindow brackets its own
// load / dispatch / input with ScopedScriptRoot so the player's context
// is restored on scope exit.  Single-window sessions never switch root,
// so this is a no-op for them.
void        setActiveScriptRoot(const void *rootKey);
const void *activeScriptRoot();
// Whether a root is still live (the active root, or a saved snapshot in
// the root registry).  Async VM entries (Maki timers) use this to refuse
// firing for a window that has been torn down (skin switch).
bool        scriptRootAlive(const void *rootKey);
// Drop a window's per-root snapshot once its runtime has been torn down.
void        dropScriptRoot(const void *rootKey);
struct ScopedScriptRoot {
    explicit ScopedScriptRoot(const void *rootKey);
    ~ScopedScriptRoot();
    ScopedScriptRoot(const ScopedScriptRoot &) = delete;
    ScopedScriptRoot &operator=(const ScopedScriptRoot &) = delete;
private:
    const void *m_prev;
};

}  // namespace qtWasabi
