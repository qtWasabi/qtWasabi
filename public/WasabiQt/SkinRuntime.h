#pragma once
//
// SkinRuntime — owns Maki script execution against a resolved widget
// tree.  M13 scaffold.  Each widget gets a `WidgetScriptObject*`
// (created via maki-bridge); each `<script file=…/>` reference in
// the skin XML gets loaded into the opensourced VCPU; per-script `param=`
// tokens drive the variable→widget binding.
//
// Foundation only at this stage — wires the data flow but doesn't
// yet make scripts visibly drive widget state.  Iterations:
//
//   M13a  load .maki blobs + bind WidgetScriptObjects                ← here
//   M13b  real SOM/ObjectTable bodies + run onScriptLoaded
//   M13c  setXmlParam / findObject / getAutoWidth wired into widgets
//   M13d  remove static `runKnownScripts` titlebar hack — let the
//         real titlebar.maki do it

#include <QtCore/qglobal.h>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

namespace WasabiQt {

namespace SkinXml { struct Document; }
namespace Layout  { struct ResolvedWidget; }

class SkinRuntime {
public:
    SkinRuntime();
    ~SkinRuntime();

    // Walk `root`, create a WidgetScriptObject for every widget that
    // has an id, then load every `<script file=…/>` referenced in
    // `doc`.  Bindings run on first call and on every reload.
    int loadScripts(const SkinXml::Document &doc,
                    Layout::ResolvedWidget &root);

    // Fire the System.onScriptLoaded handler for every loaded script.
    // Returns the number of scripts where dispatch actually started
    // (i.e. their DLF table contains an "onScriptLoaded" entry AND
    // the bound SystemObject has a matching event entry).
    //
    // BIG WARNING (M13b): this WILL hit unimplemented stubs partway
    // through real script execution.  Several SOM / GuiObject / Group
    // method bodies still return defaults, and the opensourced VM hits
    // ASSERTs (or worse) once those defaults violate an invariant.
    // Run with WASABIQT_FATAL_ASSERTS=0 (the default) to log rather
    // than abort.  Stable use needs M13c — real method bodies for
    // setXmlParam / findObject / getAutoWidth / etc.
    int dispatchOnScriptLoaded();

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

// M14c: register a callback the runtime fires whenever a script
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

}  // namespace WasabiQt
