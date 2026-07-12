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
#include <QString>

#include <qtWasabi/PlayerHost.h>
#include <qtWasabi/SkinQuickItem.h>
#include <qtWasabi/SkinRuntime.h>
#include <qtWasabi/SkinXml.h>

class QFileSystemWatcher;
class QMenu;
class QTimer;

namespace qtWasabi {
class SkinView;
}

namespace qtWasabi::head {

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

    // Headless self-test (WASABIQT_SELFTEST_CHROME=<themeName>) for the
    // theme plumbing that can't be screenshotted: the menu-bar
    // prev/next ring and the menu/dialog re-tint.  Prints PASS/FAIL.
    void runChromeSelfTest(const QString &themeName);

protected:
    // A new skin document was adopted (initial load or reload).
    virtual void skinDocumentChanged() {}
    // The old document's dependent resources must die (fires during
    // reloadSkin, after the new document is adopted and base-owned
    // subwindows are gone).
    virtual void aboutToReloadSkin() {}
    // Window title for a secondary container window.
    virtual QString subwindowTitle(const QString &containerId) const {
        return containerId;
    }

    PlayerHost *m_host = nullptr;
    SkinXml::Document m_doc;
    QHash<QString, SkinView *> m_subwindows;
    QString m_rootContainerId = QStringLiteral("main");
    SkinRuntime *m_runtime = nullptr;

private:
    QString m_settingsFile;
    QFileSystemWatcher *m_skinWatcher = nullptr;
    QTimer *m_reloadDebounce = nullptr;
    QString m_hotReloadRoot;
};

}  // namespace qtWasabi::head
