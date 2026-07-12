// HeadMenu implementation (V5d) — the Winamp-parity menu skeleton,
// moved verbatim from the reference embedder; embedder items ride on
// contributeMenu/handleMenuAction, file-pick flows route through the
// capability-gated ejectFlow/headPickFiles.
//
// Dispatch protocol: every selectable action carries its id in
// QAction::data(); after exec the id goes to handleMenuAction first
// (embedder first refusal, including framework ids like
// wa5:help.about), then to the framework switch.  Menus are rebuilt
// on every popup — checked/enabled are build-time snapshots.
#include <qtWasabi/head/HeadMenu.h>

#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <cstdlib>
#include <functional>

#include <qtWasabi/PlayerHost.h>
#include <qtWasabi/Widget.h>
#include <qtWasabi/head/HeadChrome.h>
#include <qtWasabi/head/HeadWindow.h>

namespace qtWasabi::head {

// ── MenuBuilder ──────────────────────────────────────────────────────

void MenuBuilder::addItem(const MenuItem &item) {
    if (item.id == QLatin1String("-")) {   // separator sentinel
        m_menu->addSeparator();
        return;
    }
    if (!item.children.isEmpty()) {
        QMenu *sub = m_menu->addMenu(item.label);
        sub->setStyleSheet(m_style);
        MenuBuilder child(sub, m_style);
        for (const MenuItem &c : item.children) child.addItem(c);
        return;
    }
    QAction *a = m_menu->addAction(item.label);
    a->setData(item.id);
    a->setEnabled(item.enabled);
    if (item.checkable) {
        a->setCheckable(true);
        a->setChecked(item.checked);
    }
}

void MenuBuilder::addSeparator() { m_menu->addSeparator(); }

namespace {

// Left/Right arrow keys walk the skin menu-bar's prev/next chain while
// a popup is open.  Installed on qApp (not the menu): on Wayland the
// xdg-popup may not hold keyboard focus, so a filter on the menu can
// miss the arrows.
class MenuArrowFilter : public QObject {
public:
    QMenu *menu = nullptr;
    std::function<void()> onPrev, onNext;
    bool eventFilter(QObject *o, QEvent *e) override {
        if (e->type() != QEvent::KeyPress) return false;
        auto *ke = static_cast<QKeyEvent *>(e);
        if (::getenv("WASABIQT_TRACE_MAKI"))
            fprintf(stderr, "[menukey] key=0x%x on %s\n", ke->key(),
                    o ? o->metaObject()->className() : "?");
        if (ke->key() == Qt::Key_Right) {
            // A highlighted submenu opens on Right — don't navigate then.
            if (menu && menu->activeAction() && menu->activeAction()->menu())
                return false;
            if (onNext) onNext();
            return true;
        }
        if (ke->key() == Qt::Key_Left) {
            // An open submenu closes on Left — don't navigate then.
            if (menu)
                for (QMenu *sub : menu->findChildren<QMenu *>())
                    if (sub->isVisible()) return false;
            if (onPrev) onPrev();
            return true;
        }
        return false;
    }
};

// Skeleton item helper: label + id (+ flags) with the id in data().
QAction *addId(QMenu *m, const char *label, const char *id,
               bool enabled = true, bool checkable = false,
               bool checked = false) {
    QAction *a = m->addAction(QString::fromUtf8(label));
    a->setData(QString::fromLatin1(id));
    a->setEnabled(enabled);
    if (checkable) {
        a->setCheckable(true);
        a->setChecked(checked);
    }
    return a;
}

}  // namespace


namespace {

// Test hook: WASABIQT_TEST_HEADMENU_PICK=<action-id> selects an item by
// id instead of exec()ing the popup (nested QMenu loops never return
// offscreen).  Fires once per process so a click-driven test can pick
// deterministically.  Distinct from WASABIQT_TEST_MENU_PICK, which
// belongs to the Maki PopupMenu bridge (int command ids).
QAction *findActionById(QMenu *menu, const QString &id) {
    const auto actions = menu->actions();
    for (QAction *a : actions) {
        if (!a) continue;
        if (a->menu()) {
            if (QAction *hit = findActionById(a->menu(), id)) return hit;
            continue;
        }
        if (a->data().toString() == id) return a;
    }
    return nullptr;
}

bool takeHeadMenuPick(QMenu *menu, QAction **out) {
    static bool used = false;
    const QByteArray pick = qgetenv("WASABIQT_TEST_HEADMENU_PICK");
    if (pick.isEmpty() || used) return false;
    used = true;
    *out = findActionById(menu, QString::fromUtf8(pick));
    fprintf(stderr, "[headmenu] test pick '%s' -> %s\n", pick.constData(),
            *out ? "found" : "MISSING");
    return true;
}

}  // namespace

// ── file-pick flows (capability-gated) ───────────────────────────────

QList<QUrl> HeadWindow::headPickFiles(bool folder, bool enqueueOnly) {
    const HostCapabilities caps = m_host->hostCapabilities();
    if (caps.providesFilePicker) {
        return folder ? m_host->openFolderAndEnqueue(nullptr, enqueueOnly)
                      : m_host->openFilesAndEnqueue(nullptr, enqueueOnly);
    }
    if (!caps.localFiles) return {};  // remote: never a local dialog
#ifdef Q_OS_WASM
    return {};
#else
    // The head-side dialog (the engine's pickFile default died with
    // V5d): same prompt the engine used to own.
    QList<QUrl> picked;
    if (folder) {
        const QString dir = QFileDialog::getExistingDirectory(
            nullptr, QStringLiteral("Open folder"),
            QStandardPaths::writableLocation(
                QStandardPaths::MusicLocation));
        if (dir.isEmpty()) return {};
        // Enqueue the folder's audio files (name-sorted), not the raw
        // directory URL — a player can't decode a directory.  Hosts
        // that want album-tag ordering own their own picker.
        static const QStringList kAudioExts = {
            "mp3", "ogg", "flac", "wav", "m4a", "aac", "opus", "wma",
            "aiff", "alac"};
        QStringList files;
        QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            if (kAudioExts.contains(QFileInfo(f).suffix().toLower()))
                files << f;
        }
        files.sort();
        for (const QString &f : std::as_const(files))
            picked << QUrl::fromLocalFile(f);
    } else {
        const QList<QUrl> us = QFileDialog::getOpenFileUrls(
            nullptr, QStringLiteral("Open audio file"),
            QUrl::fromLocalFile(QStandardPaths::writableLocation(
                QStandardPaths::MusicLocation)),
            QStringLiteral(
                "Audio files (*.mp3 *.ogg *.flac *.wav *.m4a *.aac "
                "*.opus *.wma);;All files (*)"));
        picked = us;
    }
    bool first = true;
    for (const QUrl &u : std::as_const(picked)) {
        if (u.isEmpty()) continue;
        m_host->enqueueAndPlay(u, enqueueOnly || !first);
        first = false;
    }
    return picked;
#endif
}

void HeadWindow::ejectFlow() {
    const HostCapabilities caps = m_host->hostCapabilities();
    if (caps.providesFilePicker) {
        // The host owns its dialogs (and its recent-files bookkeeping);
        // the returned URL is deliberately unused — EJECT's contract is
        // "prompt and play", not "report".
        m_host->pickFile(nullptr);
        return;
    }
    headPickFiles(/*folder=*/false);
}

// ── context menu ─────────────────────────────────────────────────────

void HeadWindow::showContextMenu(QPoint globalPos) {
    const QString menuStyle = themedMenuStyle();

    QMenu menu;
    menu.setStyleSheet(menuStyle);
    const QString menuId = QStringLiteral("context");
    auto anchor = [&](QMenu *at, const char *name) {
        MenuBuilder b(at, menuStyle);
        contributeMenu(menuId, QString::fromLatin1(name), b);
    };

    // === Winamp-style main menu (mirrors the classic top menu) ===

    // -- Play submenu --
    // File-pick items track whether the host can pick at all (a remote
    // player advertises localFiles=false); Play location is URL-based,
    // always available.
    const HostCapabilities caps = m_host->hostCapabilities();
    const bool canPick = caps.localFiles || caps.providesFilePicker;
    QMenu *playMenu = menu.addMenu("Play");
    playMenu->setStyleSheet(menuStyle);
    addId(playMenu, "Play file(s)...\tL", "wa5:play.files", canPick);
    addId(playMenu, "Play folder...\tShift+L", "wa5:play.folder", canPick);
    addId(playMenu, "Play location...\tCtrl+L", "wa5:play.location");
    playMenu->addSeparator();
    anchor(playMenu, "context.play.end");   // qtamp: Recent files

    anchor(&menu, "context.afterPlay");     // qtamp: Bookmarks

    menu.addSeparator();

    // -- Options submenu --
    QMenu *optMenu = menu.addMenu("Options");
    optMenu->setStyleSheet(menuStyle);
    const bool aotOn =
        window() && (window()->flags() & Qt::WindowStaysOnTopHint);
    addId(optMenu, "Always on top\tCtrl+T", "wa5:options.aot",
          true, true, aotOn);
    addId(optMenu, "Double size\tCtrl+D", "wa5:options.dsize",
          false, true);
    addId(optMenu, "Windowshade mode\tCtrl+W", "wa5:options.shade",
          false, true);
    optMenu->addSeparator();
    addId(optMenu, "Preferences...\tCtrl+P", "wa5:options.prefs");
    optMenu->addSeparator();
    addId(optMenu, "Stop after current", "wa5:options.stopafter",
          false, true);
    anchor(optMenu, "context.options.end");

    // -- Playback submenu --
    QMenu *pbMenu = menu.addMenu("Playback");
    pbMenu->setStyleSheet(menuStyle);
    addId(pbMenu, "Jump to time...\tJ", "wa5:playback.jumptime");
    addId(pbMenu, "Jump to file...\tCtrl+J", "wa5:playback.jumpfile",
          false);
    pbMenu->addSeparator();
    addId(pbMenu, "Shuffle", "wa5:playback.shuffle", false, true);
    QMenu *repMenu = pbMenu->addMenu("Repeat");
    repMenu->setStyleSheet(menuStyle);
    // "Off" is enabled + checked but dispatches nowhere — Winamp parity
    // for a player without repeat modes yet (selecting it is a no-op).
    addId(repMenu, "Off", "wa5:repeat.off", true, true, true);
    addId(repMenu, "Repeat all", "wa5:repeat.all", false, true);
    addId(repMenu, "Repeat track", "wa5:repeat.one", false, true);
    anchor(pbMenu, "context.playback.end");

    // -- Windows submenu --
    // All disabled placeholders here (the WA5:Windows menu-bar popup
    // is the wired variant); kept divergent deliberately.
    QMenu *winMenu = menu.addMenu("Windows");
    winMenu->setStyleSheet(menuStyle);
    addId(winMenu, "Equalizer\tAlt+G", "wa5:windows.eq", false, true);
    addId(winMenu, "Playlist editor\tAlt+E", "wa5:windows.pl",
          false, true);
    addId(winMenu, "Video window", "wa5:windows.vid", false, true);
    addId(winMenu, "Media library\tAlt+L", "wa5:windows.ml",
          false, true);
    winMenu->addSeparator();
    anchor(winMenu, "context.windows.end");  // qtamp: Milkdrop entry

    // -- Visualization submenu --
    QMenu *visMenu = menu.addMenu("Visualization");
    visMenu->setStyleSheet(menuStyle);
    static const char *const kVisLabels[] = {
        "Off", "Spectrum analyzer", "Oscilloscope", "VU meter"};
    for (int i = 0; i < 4; ++i) {
        addId(visMenu, kVisLabels[i],
              QByteArray("wa5:vis." + QByteArray::number(i)).constData(),
              true, true, m_visMode == i);
    }
    visMenu->addSeparator();
    anchor(visMenu, "context.visualization.end");  // qtamp: Milkdrop...

    // -- Time display submenu --
    // Elapsed vs remaining (countdown), the two modes real Winamp
    // offers.  Same per-skin slot the skin's own time-click toggle
    // and the Preferences radios use, so all three stay one state.
    QMenu *timeMenu = menu.addMenu("Time display");
    timeMenu->setStyleSheet(menuStyle);
    const int timeMode = timeDisplayMode();
    addId(timeMenu, "Time elapsed", "wa5:time.elapsed",
          true, true, timeMode != 2);
    addId(timeMenu, "Time remaining", "wa5:time.remaining",
          true, true, timeMode == 2);

    menu.addSeparator();
    addId(&menu, "About Winamp...", "wa5:help.about");
    menu.addSeparator();
    addId(&menu, "Exit", "wa5:exit");
    anchor(&menu, "context.top.end");

    // === Handle selection ===
    prepareMenuForWayland(menu);
    if (qEnvironmentVariableIsSet("WASABIQT_DUMP_MENU")) {
        dumpMenuTree(menu, QStringLiteral("context"));
        return;
    }
    QAction *picked = nullptr;
    if (takeHeadMenuPick(&menu, &picked)) {
        if (picked) dispatchMenuAction(picked);
        return;
    }
    QAction *sel = menu.exec(globalPos);
    if (!sel) return;
    dispatchMenuAction(sel);
}

// ── WA5 menu-bar popups ──────────────────────────────────────────────

const Widget *HeadWindow::openMenuBarMenu(const QString &menuId,
                                          QPoint globalPos,
                                          const Widget *source) {
    const QString menuStyle = themedMenuStyle();
    QMenu menu;
    menu.setStyleSheet(menuStyle);
    // Match on the trailing menu name so "WA5:File" and a bare "File"
    // resolve the same.
    const QString m = menuId.section(QChar(':'), -1).toLower();
    if (::getenv("WASABIQT_TRACE_MAKI"))
        fprintf(stderr, "[wa5menu] spawn '%s' (-> '%s') at (%d,%d)\n",
                menuId.toLocal8Bit().constData(),
                m.toLocal8Bit().constData(), globalPos.x(), globalPos.y());

    const QString wa5Id = QStringLiteral("wa5:") + m;
    auto anchor = [&](const char *suffix) {
        MenuBuilder b(&menu, menuStyle);
        contributeMenu(wa5Id, wa5Id + QLatin1String(suffix), b);
    };
    if (m == QLatin1String("file")) {
        addId(&menu, "Play file...", "wa5:play.file1");
        addId(&menu, "Play location...", "wa5:play.location");
        menu.addSeparator();
        addId(&menu, "Exit", "wa5:exit");
        anchor(".end");
    } else if (m == QLatin1String("play")) {
        const bool playing = m_host->isPlaying();
        addId(&menu, playing ? "Pause" : "Play",
              playing ? "wa5:transport.pause" : "wa5:transport.play");
        addId(&menu, "Stop", "wa5:transport.stop");
        menu.addSeparator();
        addId(&menu, "Previous", "wa5:transport.prev");
        addId(&menu, "Next", "wa5:transport.next");
        anchor(".end");
    } else if (m == QLatin1String("options")) {
        addId(&menu, "Preferences...", "wa5:options.prefs");
        menu.addSeparator();
        // Real Winamp's Options menu carries the two time-display
        // modes; same per-skin slot as the context-menu submenu.
        const int tm = timeDisplayMode();
        addId(&menu, "Time elapsed", "wa5:time.elapsed",
              true, true, tm != 2);
        addId(&menu, "Time remaining", "wa5:time.remaining",
              true, true, tm == 2);
        anchor(".end");
    } else if (m == QLatin1String("view") || m == QLatin1String("windows")) {
        addId(&menu, "Playlist editor", "wa5:windows.toggle.pl");
        addId(&menu, "Media library", "wa5:windows.toggle.ml");
        addId(&menu, "Video", "wa5:windows.toggle.vid");
        menu.addSeparator();
        QMenu *visM = menu.addMenu("Visualization");
        visM->setStyleSheet(menuStyle);
        static const char *const vl[] = {"Off", "Spectrum analyzer",
                                         "Oscilloscope", "VU meter"};
        for (int i = 0; i < 4; ++i) {
            addId(visM, vl[i],
                  QByteArray("wa5:vis." + QByteArray::number(i))
                      .constData(),
                  true, true, m_visMode == i);
        }
        anchor(".end");
    } else if (m == QLatin1String("help")) {
        addId(&menu, "About...", "wa5:help.about");
        anchor(".end");
    } else {
        showContextMenu(globalPos);
        return nullptr;
    }

    if (qEnvironmentVariableIsSet("WASABIQT_DUMP_MENU")) {
        dumpMenuTree(menu, QStringLiteral("wa5:") + m);
        return nullptr;
    }
    QAction *picked = nullptr;
    if (takeHeadMenuPick(&menu, &picked)) {
        if (picked) dispatchMenuAction(picked);
        return nullptr;
    }

    // menugroup hover-switch: while this popup is open, poll the cursor
    // and chain to a sibling menu button it enters.  Record where the
    // cursor was at spawn and only switch once it has actually MOVED
    // (Wasabi's xuimenu timerCheck does the same) — otherwise a
    // stationary cursor still hovering the just-clicked / arrow-key'd
    // button would immediately yank the popup back to it.
    const Widget *chainTo = nullptr;
    // Snapshot the sweep attrs at spawn: a menu action (Preferences →
    // reloadSkin) can rebuild the widget tree during exec and free
    // `source`, so the arrow-key chain must not deref it afterwards.
    const QString grp = source
        ? source->attrs.value(QStringLiteral("menugroup")) : QString();
    const QString prevId = source
        ? source->attrs.value(QStringLiteral("prev")) : QString();
    const QString nextId = source
        ? source->attrs.value(QStringLiteral("next")) : QString();
    const QPoint origCursor = QCursor::pos();
    QTimer poll;
    poll.setInterval(60);
    connect(&poll, &QTimer::timeout, &menu, [&] {
        if (!source || grp.isEmpty()) return;
        const QPoint gc = QCursor::pos();
        if (gc == origCursor) return;   // cursor hasn't moved yet
        const QPoint ip = mapFromGlobal(gc).toPoint();
        const Widget *sib = menuWidgetAt(ip);
        if (sib && sib != source &&
            sib->attrs.value(QStringLiteral("menugroup"))
                .compare(grp, Qt::CaseInsensitive) == 0) {
            chainTo = sib;
            menu.close();
        }
    });
    // Left/Right arrow keys walk the prev/next menu chain.
    MenuArrowFilter navFilter;
    navFilter.menu = &menu;
    auto chainByAttr = [&](const QString &id) {
        if (id.isEmpty()) return;
        Widget *nw = Widget::findById(id);
        if (nw && nw->tag == QLatin1String("menu")) {
            chainTo = nw;
            menu.close();
        }
    };
    navFilter.onPrev = [&] { chainByAttr(prevId); };
    navFilter.onNext = [&] { chainByAttr(nextId); };
    // App-level filter (not menu-level): on Wayland the xdg-popup may
    // not hold keyboard focus, so a filter on the menu can miss the
    // arrows.  qApp sees the key wherever Qt delivers it.  MUST be
    // removed before navFilter (a local) is destroyed.
    if (source) qApp->installEventFilter(&navFilter);

    if (source) poll.start();
    prepareMenuForWayland(menu);
    QAction *sel = menu.exec(globalPos);
    poll.stop();
    if (source) qApp->removeEventFilter(&navFilter);
    if (chainTo) return chainTo;          // chain; don't run the action
    if (sel) dispatchMenuAction(sel);
    return nullptr;
}

// ── dispatch ─────────────────────────────────────────────────────────

void HeadWindow::dispatchMenuAction(QAction *sel) {
    const QString id = sel->data().toString();
    if (id.isEmpty()) return;
    // Embedder first refusal — including framework ids (qtamp serves
    // wa5:help.about with its own About dialog).
    if (handleMenuAction(id)) return;

    if (id == QLatin1String("wa5:play.files")) {
        // Multi-select; the host's picker owns recent-file
        // bookkeeping.
        headPickFiles(/*folder=*/false);
    } else if (id == QLatin1String("wa5:play.folder")) {
        headPickFiles(/*folder=*/true);
    } else if (id == QLatin1String("wa5:play.file1")) {
        // The WA5 File menu's single "Play file..." — EJECT semantics.
        ejectFlow();
    } else if (id == QLatin1String("wa5:play.location")) {
        bool ok = false;
        const QString url = QInputDialog::getText(
            nullptr, tr("Open location"),
            tr("Enter a URL or stream address:"), QLineEdit::Normal,
            QString(), &ok);
        if (ok && !url.isEmpty()) m_host->enqueueAndPlay(QUrl(url));
    } else if (id == QLatin1String("wa5:options.aot")) {
        if (auto *w = window()) {
            Qt::WindowFlags f = w->flags();
            if (sel->isChecked()) f |=  Qt::WindowStaysOnTopHint;
            else                  f &= ~Qt::WindowStaysOnTopHint;
            w->setFlags(f);
            w->show();
        }
    } else if (id == QLatin1String("wa5:options.prefs")) {
        // Embedder dialog first; the framework Preferences dialog
        // lands in V5e.
        showPreferences();
    } else if (id == QLatin1String("wa5:playback.jumptime")) {
        bool ok = false;
        QString timeStr = QInputDialog::getText(nullptr,
            "Jump to Time",
            "Enter time (MM:SS or seconds):",
            QLineEdit::Normal, "", &ok);
        if (ok && !timeStr.isEmpty()) {
            qint64 jumpMs = 0;
            if (timeStr.contains(':')) {
                QStringList parts = timeStr.split(':');
                if (parts.size() >= 2)
                    jumpMs =
                        (parts[0].toInt() * 60 + parts[1].toInt()) * 1000;
            } else {
                jumpMs = timeStr.toInt() * 1000;
            }
            m_host->seekMs(qBound(qint64(0), jumpMs, m_host->durationMs()));
        }
    } else if (id == QLatin1String("wa5:transport.play")) {
        m_host->play();
    } else if (id == QLatin1String("wa5:transport.pause")) {
        m_host->pause();
    } else if (id == QLatin1String("wa5:transport.stop")) {
        m_host->stop();
    } else if (id == QLatin1String("wa5:transport.prev")) {
        m_host->prev();
    } else if (id == QLatin1String("wa5:transport.next")) {
        m_host->next();
    } else if (id == QLatin1String("wa5:windows.toggle.pl")) {
        toggleSubwindow(QStringLiteral("pl"));
    } else if (id == QLatin1String("wa5:windows.toggle.ml")) {
        toggleSubwindow(QStringLiteral("ml"));
    } else if (id == QLatin1String("wa5:windows.toggle.vid")) {
        toggleSubwindow(QStringLiteral("vid"));
    } else if (id.startsWith(QLatin1String("wa5:vis."))) {
        setVisMode(id.mid(8).toInt());
    } else if (id == QLatin1String("wa5:time.elapsed")) {
        setTimeDisplayMode(1);
    } else if (id == QLatin1String("wa5:time.remaining")) {
        setTimeDisplayMode(2);
    } else if (id == QLatin1String("wa5:exit")) {
        if (window()) window()->close();
    }
    // Unmatched ids (wa5:repeat.off — Winamp-parity no-op — and any
    // embedder id the override declined) fall through silently.
}

// ── helpers ──────────────────────────────────────────────────────────

const Widget *HeadWindow::menuWidgetAt(QPoint itemPos) const {
    const Widget *found = nullptr;
    std::function<void(const Widget &)> walk = [&](const Widget &w) {
        if (found) return;
        if (w.tag == QLatin1String("menu") &&
            w.attrs.value(QStringLiteral("visible")) !=
                QStringLiteral("0") &&
            w.lastCanvasRect.contains(itemPos)) {
            found = &w;
            return;
        }
        for (const auto &c : w.children) if (c) walk(*c);
    };
    walk(tree());
    return found;
}

void HeadWindow::dumpMenusForGate() {
    showContextMenu(QPoint(0, 0));
    const char *ids[] = {"WA5:File", "WA5:Play", "WA5:Options",
                         "WA5:Windows", "WA5:View", "WA5:Help",
                         "WA5:Bogus"};  // Bogus proves the
                                        // unknown-id → context fallback
    for (const char *id : ids)
        openMenuBarMenu(QString::fromLatin1(id), QPoint(0, 0), nullptr);
}

}  // namespace qtWasabi::head
