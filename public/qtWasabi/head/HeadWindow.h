// HeadWindow — the head framework's window base over SkinQuickItem.
//
// Absorbs the generic core every head shares: skin-document ownership,
// full skin (re)load with Maki VM reset, hot-reload watching, the
// colour-theme preference + synthetic-theme injection, root-container
// selection, chrome re-tinting, the Maki runtime handle with the
// host-metadata resolver, and the headless chrome self-test.
//
// Embedder-specific behavior hangs off virtual hooks:
//   - skinDocumentChanged(): a new document was adopted (rewire
//     overlays that reference widgets of the previous tree).
//   - aboutToReloadSkin(): resources of the OLD document (subwindows,
//     cached views) must die before the new one loads.
//
// Head-local state lives in a settings file the embedder injects
// (setSettingsFile) — winamp.conf for qtamp, head.ini for the
// reference head.  (V5b step 1 of 3.)
#pragma once

#include <QHash>
#include <QPoint>
#include <QRect>
#include <QString>

#include <qtWasabi/PlayerHost.h>
#include <qtWasabi/SkinQuickItem.h>
#include <qtWasabi/SkinRuntime.h>
#include <qtWasabi/SkinXml.h>
#include <qtWasabi/head/HeadMenu.h>

class QAction;
class QFileSystemWatcher;
class QMenu;
class QUrl;
class QMouseEvent;
class QKeyEvent;
class QWheelEvent;
class QPainter;
class QTimer;

namespace qtWasabi {
class SkinView;
}

namespace qtWasabi::head {

class HeadPreferences;

// Image-size callback for Layout::hitTest / alphaHitTestList
// (`userdata` = a BitmapRegistry*).  Handles the NStatesButton `<id>0`
// naming fallback so state buttons without explicit w/h stay hittable.
QSize imageSizeForHitTest(const QString &bitmapId, void *userdata);

class HeadWindow : public SkinQuickItem {
    Q_OBJECT
public:
    explicit HeadWindow(PlayerHost *host, QQuickItem *parent = nullptr);

    // Head-local settings (colour theme, window prefs, ...).  Set it
    // before the first skin load; empty = no persistence.
    void setSettingsFile(const QString &path) { m_settingsFile = path; }
    QString settingsFile() const { return m_settingsFile; }

    // Keep the parsed skin Document around so secondary windows
    // (EQ / Playlist) can load other containers from the same skin
    // on demand.
    void setSkinDocument(SkinXml::Document doc);
    const SkinXml::Document &skinDocument() const { return m_doc; }

    // Apply the player's colour theme (Wasabi "gammaset") after a skin
    // load; injects the synthetic per-role recolor themes and honours
    // the saved choice (WASABIQT_COLORTHEME overrides for tests).
    void applyPreferredColorTheme();

    // The container this window renders as its root.  "main" is the
    // classic player; a head can render another container instead
    // (--container), e.g. the Playlist Editor — one container per
    // instance, which is how the browser iframes each present a
    // single window.
    void setRootContainerId(const QString &id) { m_rootContainerId = id; }
    QString rootContainerId() const { return m_rootContainerId; }
    bool isMainRoot() const {
        return m_rootContainerId.compare(QStringLiteral("main"),
                                         Qt::CaseInsensitive) == 0;
    }

    // Toggle a secondary container window (EQ / Playlist / etc.).
    // Creates the SkinView lazily on first call.  Layout id matches
    // the skin XML convention — modern skins almost always use
    // "normal" as the default layout name.
    void toggleSubwindow(const QString &containerRef);

    // Lazily create + load a container's SkinView WITHOUT changing its
    // visibility (toggleSubwindow owns show/hide).  Returns the slot,
    // or nullptr if the container failed to load.  Also used by
    // offscreen container-capture paths.
    SkinView *ensureSubwindow(const QString &containerRef);

    // Like ensureSubwindow but never creates — for visibility queries
    // (Maki isNamedWindowVisible at script init must not instantiate
    // every queried window).
    SkinView *peekSubwindow(const QString &containerRef) const;

    // Full skin swap: parse, expand, adopt, re-theme, resize the OS
    // window to the new native size, reset + reload the Maki VM.
    // Subwindows of the old document are torn down (they are
    // base-owned); embedder extras die in aboutToReloadSkin().
    void reloadSkin(const QString &skinXmlPath);

    // Hot-reload: watch every XML/script file under the skin directory
    // and trigger reloadSkin ~250 ms after the last save.  Idempotent —
    // calling again with a new root path swaps the watcher's directory.
    void installHotReloadWatcher(const QString &rootXmlPath);

    // The Maki runtime, for the OS-resize handler to re-fire onResize.
    SkinRuntime *skinRuntime() const { return m_runtime; }
    // Hand the SkinRuntime to the window so reloadSkin can reset it;
    // wires the host-metadata resolver the file-info scripts read.
    void setSkinRuntime(SkinRuntime *r);

    // (Re)fire System.onTitleChange on every loaded script so the
    // skin's fileinfo.maki reloads the track-info display.  Safe to
    // call before a skin/runtime exists (no-op).
    void fireTitleChange();

    // Chrome styling (HeadChrome over this window's registries).
    QString themedMenuStyle();
    QString menuStyleFor(const QString &sel);
    QString themedDialogStyle();
    void restyleOpenChrome();
    void prepareMenuForWayland(QMenu &menu);
    void setActiveGammaset(const QString &name) override;

    // Append embedder pages to the framework Preferences dialog.
    virtual void contributePrefPages(HeadPreferences &dlg) {
        Q_UNUSED(dlg);
    }
    // Live-switch to another backend (Connection page "Use").  Return
    // false when the head cannot reconnect at runtime — the choice is
    // persisted and applies on the next start.  Orchestrated live
    // switching (pausing the local player on switch-away) lands in V6.
    virtual bool connectToBackend(const QString &url,
                                  const QString &bearerToken) {
        Q_UNUSED(url);
        Q_UNUSED(bearerToken);
        return false;
    }

    // Headless menu-dump gate driver: build every menu this head can
    // show and print their trees (WASABIQT_DUMP_MENU short-circuits
    // the exec inside each builder).
    void dumpMenusForGate();

    // The visible <Menu> widget whose canvas rect contains `itemPos`.
    const Widget *menuWidgetAt(QPoint itemPos) const;

    // Headless self-test (WASABIQT_SELFTEST_CHROME=<themeName>) for the
    // theme plumbing that can't be screenshotted: the menu-bar
    // prev/next ring and the menu/dialog re-tint.  Prints PASS/FAIL.
    void runChromeSelfTest(const QString &themeName);

    // ── Visualisation mode (head-local) ───────────────────────────────
    //    0=Off, 1=Spectrum (default), 2=Oscilloscope, 3=VU meter.
    int  visMode() const { return m_visMode; }
    void setVisMode(int m);

    // ── Time-display mode (skin-level) ────────────────────────────────
    //    1 = elapsed, 2 = remaining (countdown).  One state shared
    //    with the skin scripts through the per-skin
    //    TimerElapsedRemaining slot, nudged via onTitleChange.
    int  timeDisplayMode() const;
    void setTimeDisplayMode(int mode);

    // ── Colour-themes list state (head-local) ─────────────────────────
    int     colorThemesSelectedRow() const { return m_ctSelectedRow; }
    void    setColorThemesSelectedRow(int row) {
        m_ctSelectedRow = row;
        update();
    }
    int     colorThemesTopRow() const { return m_ctTopRow; }
    void    setColorThemesTopRow(int row) {
        m_ctTopRow = row;
        update();
    }
    QRect   colorThemesListRect() const { return m_ctListRect; }

    // Perform a widget's `action=` exactly as a real click would — the
    // engine calls this (registerSkinWidgetClickCallback) from Maki's
    // GuiObject.leftClick()/rightClick() delegation.
    bool triggerWidgetActionById(const QString &id, bool right);

    // Drawer machinery (canonical Wasabi drawer/config-tab ids) —
    // engine-interim fixups that mirror configtabs.m / videoavs.m
    // until the Maki Timer chain drives them end to end.
    void mousePressEventForTab(int tab) {
        switchDrawerTab(tab);
        update();
    }
    void setDrawerOpen(bool open);
    void switchDrawerTab(int tab);
    void applyDrawerModeFixup(const QString &clickedId);

protected:
    // A new skin document was adopted (initial load or reload).
    virtual void skinDocumentChanged() {}
    // The old document's dependent resources must die (fires during
    // reloadSkin, after the new document is adopted and base-owned
    // subwindows are gone).
    virtual void aboutToReloadSkin() {}
    // Selected-menu-action dispatch: embedder first refusal
    // (handleMenuAction), then the framework id switch.
    void dispatchMenuAction(QAction *sel);

    // Window title for a secondary container window.
    virtual QString subwindowTitle(const QString &containerId) const {
        return containerId;
    }
    // The head's context menu (right-click fallback + SYSMENU): the
    // framework builds the Winamp-parity skeleton; embedders extend it
    // through contributeMenu/handleMenuAction.  Q_INVOKABLE so the
    // SYSMENU action (Host::showSystemMenu) can pop it by name via
    // QMetaObject::invokeMethod.
    Q_INVOKABLE virtual void showContextMenu(QPoint globalPos);
    // Embedder items at a named anchor (see HeadMenu.h).  Anchors:
    // context.play.end, context.afterPlay, context.options.end,
    // context.playback.end, context.windows.end,
    // context.visualization.end, context.top.end, wa5:<menu>.end.
    virtual void contributeMenu(const QString &menuId,
                                const QString &anchor, MenuBuilder &b) {
        Q_UNUSED(menuId);
        Q_UNUSED(anchor);
        Q_UNUSED(b);
    }
    // First refusal on every selected menu action id (framework ids
    // included).  Return true = consumed.
    virtual bool handleMenuAction(const QString &actionId) {
        Q_UNUSED(actionId);
        return false;
    }
    // Open the Preferences dialog.  The default opens the framework
    // HeadPreferences (Connection + Presentation pages); embedders
    // with their own dialog override.
    virtual bool showPreferences();

    // Per-tick hook for embedder overlays (vis surfaces): the embedder
    // calls it from its repaint tick (qtamp drives its MilkDrop overlay
    // directly for now; wired framework-side when the repaint timer
    // moves into HeadWindow).
    virtual void overlayTick() {}
    // Embedder-owned actions get first refusal before the generic
    // dispatch (vis-overlay prev/next, ...).  Return true = consumed.
    virtual bool interceptAction(const QString &action,
                                 const QString &param) {
        Q_UNUSED(action);
        Q_UNUSED(param);
        return false;
    }
    // Open the popup for a skin menu-bar button (`<Menu menu="WA5:X">`).
    // Returns the SIBLING menu widget to chain to when the cursor swept
    // onto it (the menu-bar sweep), or nullptr when the popup closed
    // normally.  The framework builds the WA5:* skeleton (unknown ids
    // fall back to the context menu).
    virtual const Widget *openMenuBarMenu(const QString &menuId,
                                          QPoint anchor,
                                          const Widget *source);

    // File-pick flows (EJECT, PLAY on empty, Ctrl+O/L, menu items):
    // hosts with providesFilePicker keep their own dialogs; otherwise
    // the head's QFileDialog serves localFiles hosts and remote hosts
    // are silently consumed (never a local dialog).
    void ejectFlow();
    QList<QUrl> headPickFiles(bool folder, bool enqueueOnly = false);

    // Engine paint hook: threads the colour-themes list state and the
    // vis mode into TreePainter (the engine stays a pure renderer).
    void paintInto(QPainter *p, const QSize &canvas) override;

    // Input dispatch: full Wasabi click protocol (script first refusal,
    // action dispatch, alpha hit-list bubbling, drawer tabs, colour-
    // themes list, window drag/edge-resize fallback).
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

    // Widget currently holding the left mouse button — receives
    // onLeftButtonUp / onMouseMove until release; id-checked against
    // the live registry so a press→rebuild→release never derefs a
    // freed widget.
    void setActiveWidget(Widget *w);
    bool activeWidgetStale() const;

    PlayerHost *m_host = nullptr;
    SkinXml::Document m_doc;
    QHash<QString, SkinView *> m_subwindows;
    QString m_rootContainerId = QStringLiteral("main");
    SkinRuntime *m_runtime = nullptr;

    // Visualisation mode (context menu → Visualization submenu).
    int  m_visMode    = 1;

    QPoint     m_dragOrigin;
    bool       m_dragging = false;
    // Script receiver whose onLeftButtonDown claimed the press —
    // mouseReleaseEvent routes the matching onLeftButtonUp to it.
    QString    m_makiPressId;
    const Widget *m_makiPressWidget = nullptr;
    Widget *m_activeWidget = nullptr;
    QString m_activeWidgetId;
    bool m_drawerOpen = true;
    // Colour-themes list state — threaded through TreePainter on each
    // paint; the engine itself stays application-state-free.
    int          m_ctSelectedRow = -1;   // -1 = use active gammaset
    mutable int  m_ctTopRow      = 0;
    mutable QRect m_ctListRect;
    // Scrollbar drag state for the colour-themes list.
    bool m_ctDragging  = false;
    int  m_ctDragOffset = 0;
    int  m_ctTrackTop  = 0;
    int  m_ctTrackBot  = 0;
    int  m_ctThumbH    = 31;
    int  m_ctMaxTop    = 0;

private:
    QString m_settingsFile;
    QFileSystemWatcher *m_skinWatcher = nullptr;
    QTimer *m_reloadDebounce = nullptr;
    QString m_hotReloadRoot;
};

}  // namespace qtWasabi::head
