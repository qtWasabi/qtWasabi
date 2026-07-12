// HeadWindow implementation — moved verbatim from the reference
// embedder's window layer (V5b step 1); embedder-specific behavior
// stays behind the skinDocumentChanged()/aboutToReloadSkin() hooks.
#include <qtWasabi/head/HeadWindow.h>

#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QMessageBox>
#include <QQuickWindow>
#include <QSettings>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <functional>

#include <qtWasabi/Layout.h>
#include <qtWasabi/Widget.h>
#include <qtWasabi/head/HeadChrome.h>

namespace qtWasabi::head {

namespace {

// Data-driven per-skin fixups for theme-less skins whose synthetic
// recolor needs a nudge — matched by skin directory name, applied
// after the synthetic themes are injected.  Keep this a TABLE: quirks
// are declarations, not code paths.
struct SkinQuirk {
    const char *skinDirContains;  // case-insensitive substring
    const char *region;           // bitmap region id
    QRect rect;
    const char *group;            // gammagroup to tint the region with
    int strength;
};

const SkinQuirk kSkinQuirks[] = {
    // HeadAMP's "Balance" and "Volume" labels are baked into the
    // EQ-drawer body bitmap, so the body tint turns them purple while
    // the neighbouring "reset" button (a separate bitmap) is themed as
    // a button.  Tint just the label glyphs with the button role so
    // they read the same as reset.
    {"HeadAMP", "bg.drawerLeft", QRect(108, 31, 149, 13), "syn.button", 80},
};

}  // namespace

HeadWindow::HeadWindow(PlayerHost *host, QQuickItem *parent)
    : SkinQuickItem(parent), m_host(host) {
    // Hand the Host to SkinQuickItem so paintInto pulls live display
    // strings AND <slider> thumb positions from it.
    setHost(host);
}

void HeadWindow::setSkinDocument(SkinXml::Document doc) {
    m_doc = std::move(doc);
    // The base-class m_doc was captured by load() as a pointer into
    // the CALLER's stack-local Document.  After we take ownership
    // here, that base pointer dangles — re-point it at the
    // now-member-owned copy so paint paths can keep dereferencing it
    // safely.
    setDocument(&m_doc);
    skinDocumentChanged();
}

// Apply the player's colour theme (Wasabi "gammaset") after a skin
// load.  Real Winamp remembers the chosen colour theme; we mirror
// that.  Critically, the Bento family ships its frame borders/bevels
// as near-black SOURCE bitmaps and relies on a `boost=1` colour
// theme to lift them to the intended grey.  The skin's auto-marked
// `*Default` gammaset is `boost=0` (an identity-ish transform) so it
// leaves that chrome flat black — which is NOT how the skin is meant
// to look.  Restore the user's saved choice; otherwise fall back to
// whatever the engine auto-selected.  Keyed off the theme NAME, not
// any per-skin widget id.
void HeadWindow::applyPreferredColorTheme() {
    // Offer the synthetic per-role recolor themes on EVERY skin (they
    // sit alongside the skin's own themes, never replace them).  Tag any
    // untagged bitmaps with a role from the widget tree, then synthesize
    // themes that recolour each gammagroup the skin actually uses (its
    // own "Backgrounds"/"Buttons"/… names plus the freshly tagged ones)
    // by that role.
    if (!gammasets().hasSyntheticStyles()) {
        // A theme-less skin opts into accenting its drawer panels (the
        // only large always-visible "spare" element on skins like
        // HeadAMP, whose side drawers are the speaker ears).
        registry().assignRolesFromWidgetTree(
            tree(), gammasets().hadNoNativeThemes());
        gammasets().injectSyntheticThemes(registry().usedGammagroups());
        registry().clearAccentRegions();
        if (gammasets().hadNoNativeThemes()) {
            for (const SkinQuirk &q : kSkinQuirks) {
                if (m_doc.skinDir.contains(
                        QLatin1String(q.skinDirContains),
                        Qt::CaseInsensitive)) {
                    registry().setRegionGroup(QLatin1String(q.region),
                                              q.rect,
                                              QLatin1String(q.group),
                                              q.strength);
                }
            }
        }
    }
    const QStringList have = gammasets().names();
    if (have.isEmpty()) return;
    QString want;
    if (!m_settingsFile.isEmpty()) {
        QSettings s(m_settingsFile, QSettings::IniFormat);
        want = s.value(QStringLiteral("player/colortheme")).toString();
    }
    // Test override: WASABIQT_COLORTHEME forces a colour theme for the
    // run (set it EMPTY to fall back to the skin's own dark auto-default
    // `*Default`).  Lets the offscreen pipeline compare against stock
    // Winamp instead of whatever synthetic recolor the user has saved.
    if (qEnvironmentVariableIsSet("WASABIQT_COLORTHEME"))
        want = qEnvironmentVariable("WASABIQT_COLORTHEME");
    // ONLY honour an explicit saved choice.  Do NOT auto-pick a grey
    // theme: the reference look is the skin's dark auto-default
    // (`*Default`), and overriding it greyed the whole player.
    if (!want.isEmpty() && have.contains(want))
        setActiveGammaset(want);
}

void HeadWindow::reloadSkin(const QString &skinXmlPath) {
    SkinXml::Document doc;
    QString err;
    if (!SkinXml::parse(skinXmlPath, doc, &err)) {
        QMessageBox::warning(nullptr, tr("Skin load failed"),
            tr("Could not parse %1:\n%2").arg(skinXmlPath, err));
        return;
    }
    if (!load(doc, m_rootContainerId, "normal", &err)) {
        QMessageBox::warning(nullptr, tr("Skin load failed"),
            tr("Layout expand failed: %1").arg(err));
        return;
    }
    setSkinDocument(doc);
    applyPreferredColorTheme();
    // Adopt the new skin's native layout size on the host window.
    // SkinQuickItem::load() updates `m_nativeSize` via the parsed
    // `<layout w h>` and we propagate that to the QQuickWindow so
    // the OS window matches.  Without this, switching from a
    // smaller skin to a wider one leaves the window at the old size
    // and the new skin's right/bottom chrome overflows the viewport.
    // Skin-agnostic: every skin gets resized to its own declared
    // native size.
    if (auto *w = window()) {
        const QSize ns = layoutNativeSize();
        if (ns.isValid() && !ns.isEmpty()) {
            w->setMinimumSize(QSize(0, 0));
            w->setMaximumSize(QSize(16777215, 16777215));
            w->resize(ns);
            setSize(QSizeF(ns));
        }
    }
    // Any previously-open resources belong to the old skin doc.
    aboutToReloadSkin();
    auto &mutableTree = const_cast<Layout::ResolvedWidget &>(tree());
    Layout::runKnownScripts(mutableTree, layoutNativeSize().width());
    // Wire stepper buttons (Decrease/Increase + Display text) to their
    // sibling cfgattrib slider.  Engine-level — works for any Wasabi
    // skin that follows the canonical naming convention.
    Layout::wireSteppers(mutableTree);
    rebuildWindowRegion();
    // Re-run the Maki VM if the embedder gave us a runtime handle.
    // reset() tears down the prior VM state (scripts, system objects,
    // widget objects, global tables) so the new skin starts clean
    // — otherwise the previous skin's scripts keep firing alongside
    // the new ones.
    if (m_runtime) {
        m_runtime->reset();
        m_runtime->loadScripts(doc, mutableTree);
        // Resolve every widget's effective pixel rect against the
        // real layout size BEFORE scripts run, so Maki getWidth()/
        // getHeight() on relat-sized groups return the true extent
        // instead of 0.
        mutableTree.cacheResolvedRects(QPoint(0, 0), layoutNativeSize());
        m_runtime->dispatchOnScriptLoaded();
        m_runtime->dispatchXuiParams(mutableTree);
        // Fire the initial onResize on every script that registered
        // a handler.  configtabs.m centres drawer.content via this
        // path; WASABIQT_NO_FIRE_RESIZE=1 skips it for offscreen
        // regression baselines that want the pre-resize chrome.
        if (!::getenv("WASABIQT_NO_FIRE_RESIZE")) {
            m_runtime->dispatchInitialResize(
                layoutNativeSize().width(),
                layoutNativeSize().height());
        }
    }
    // dispatchInitialResize (called above) drives layout growth through
    // the Maki VM's onResize fixpoint.  Just re-cache so paint +
    // hit-test see the settled geometry.
    mutableTree.cacheResolvedRects(QPoint(0, 0), layoutNativeSize());
    update();
}

void HeadWindow::installHotReloadWatcher(const QString &rootXmlPath) {
    const QString skinDir = QFileInfo(rootXmlPath).absolutePath();
    if (!m_skinWatcher) {
        m_skinWatcher = new QFileSystemWatcher(this);
        m_reloadDebounce = new QTimer(this);
        m_reloadDebounce->setSingleShot(true);
        m_reloadDebounce->setInterval(250);
        connect(m_reloadDebounce, &QTimer::timeout, this,
            [this]() {
                if (!m_hotReloadRoot.isEmpty()) {
                    fprintf(stderr,
                        "[hot-reload] reloading %s\n",
                        m_hotReloadRoot.toLocal8Bit().constData());
                    reloadSkin(m_hotReloadRoot);
                }
            });
        connect(m_skinWatcher,
                &QFileSystemWatcher::fileChanged,
                this, [this](const QString &) {
                    m_reloadDebounce->start();
                });
        connect(m_skinWatcher,
                &QFileSystemWatcher::directoryChanged,
                this, [this](const QString &) {
                    m_reloadDebounce->start();
                });
    }
    // Swap targets.
    const QStringList prevFiles = m_skinWatcher->files();
    const QStringList prevDirs  = m_skinWatcher->directories();
    if (!prevFiles.isEmpty()) m_skinWatcher->removePaths(prevFiles);
    if (!prevDirs.isEmpty())  m_skinWatcher->removePaths(prevDirs);

    m_hotReloadRoot = rootXmlPath;
    m_skinWatcher->addPath(skinDir);

    QDirIterator it(skinDir, {"*.xml", "*.maki", "*.m"},
                    QDir::Files, QDirIterator::Subdirectories);
    QStringList xmls;
    while (it.hasNext()) xmls << it.next();
    if (!xmls.isEmpty()) m_skinWatcher->addPaths(xmls);
}

void HeadWindow::setSkinRuntime(SkinRuntime *r) {
    m_runtime = r;
    if (!r || !m_host) return;
    // Pipe the host's real track metadata into the skin's Maki
    // file-info scripts.  Keys are lower-case: "playitem:string",
    // "playitem:displaytitle", "decoder", "meta:<field>".
    PlayerHost *h = m_host;
    r->setPlayItemMetadataResolver(
        [h](const QString &key) -> QString {
            if (key == QLatin1String("playitem:string"))       return h->songPath();
            if (key == QLatin1String("playitem:displaytitle")) return h->playItemDisplayTitle();
            if (key == QLatin1String("decoder"))               return h->decoderName();
            // Live data sources (otherwise unbound→0 in the VM).
            // Milliseconds — the Maki scale: getPlayItemLength()/
            // getPosition() return core ms and scripts feed them
            // straight into integerToTime(ms).
            if (key == QLatin1String("playitem:length"))
                return QString::number(h->durationMs());
            if (key == QLatin1String("playitem:position"))
                return QString::number(h->positionMs());
            if (key == QLatin1String("playlist:length"))
                return QString::number(h->playlistRowCount());
            if (key == QLatin1String("playlist:index"))
                return QString::number(h->playlistCurrentRow());
            if (key == QLatin1String("songinfo"))
                // No live multi-line song-info blob; empty is the
                // correct neutral (vs an int-0→"0" that would leak the
                // channel/bitrate guards).
                return QString();
            if (key.startsWith(QLatin1String("meta:")))
                return h->playItemMetaData(key.mid(5));
            return QString();
        });
}

void HeadWindow::fireTitleChange() {
    if (qEnvironmentVariableIntValue("WASABIQT_TRACE_META") == 1)
        qInfo("[meta] fireTitleChange: runtime=%p host=%p title='%s'",
              (void *)m_runtime, (void *)m_host,
              m_host ? m_host->playItemDisplayTitle().toLocal8Bit().constData() : "");
    if (m_runtime && m_host)
        m_runtime->dispatchTitleChange(m_host->playItemDisplayTitle());
}

QString HeadWindow::themedMenuStyle() {
    return head::themedMenuStyle(gammasets(), colors());
}

QString HeadWindow::menuStyleFor(const QString &sel) {
    return head::menuStyleFor(gammasets(), colors(), sel);
}

QString HeadWindow::themedDialogStyle() {
    return head::themedDialogStyle(gammasets(), colors());
}

void HeadWindow::restyleOpenChrome() {
    head::restyleOpenChrome(themedDialogStyle(), themedMenuStyle());
}

void HeadWindow::prepareMenuForWayland(QMenu &menu) {
    head::prepareMenuForWayland(menu, window());
}

void HeadWindow::setActiveGammaset(const QString &name) {
    SkinQuickItem::setActiveGammaset(name);
    restyleOpenChrome();
}

void HeadWindow::runChromeSelfTest(const QString &themeName) {
    fprintf(stderr, "[selftest] color themes (%d): %s\n",
            int(gammasets().names().size()),
            gammasets().names().join(QStringLiteral(", "))
                .toLocal8Bit().constData());
    // (1) menu-bar ring: every <menu> with a next/prev points at a
    // widget that still exists (this is exactly what chainByAttr does).
    QList<const Widget *> menus;
    std::function<void(const Widget &)> collect =
        [&](const Widget &w) {
        if (w.tag == QLatin1String("menu") &&
            !w.attrs.value(QStringLiteral("menu")).isEmpty())
            menus.append(&w);
        for (const auto &c : w.children) if (c) collect(*c);
    };
    collect(tree());
    int ringOk = 0, ringTot = 0;
    for (const Widget *m : menus) {
        const QString nx = m->attrs.value(QStringLiteral("next"));
        const QString pv = m->attrs.value(QStringLiteral("prev"));
        if (nx.isEmpty() && pv.isEmpty()) continue;
        ++ringTot;
        const bool nOk = nx.isEmpty() || Widget::findById(nx);
        const bool pOk = pv.isEmpty() || Widget::findById(pv);
        if (nOk && pOk) ++ringOk;
        const QRect r = m->lastCanvasRect;
        fprintf(stderr,
                "[selftest] menu '%s' rect=%d,%d,%dx%d next=%s%s prev=%s%s\n",
                m->id.toLocal8Bit().constData(),
                r.x(), r.y(), r.width(), r.height(),
                nx.toLocal8Bit().constData(), nOk ? "" : "(MISSING)",
                pv.toLocal8Bit().constData(), pOk ? "" : "(MISSING)");
    }
    fprintf(stderr, "[selftest] menu-bar ring: %d/%d resolve -> %s\n",
            ringOk, ringTot,
            (ringTot > 0 && ringOk == ringTot) ? "PASS"
                : ringTot == 0 ? "N/A (skin has no menu bar)" : "FAIL");

    // (2) chrome re-tint across a colour-theme switch.
    const QString active0 =
        gammasets().active() ? gammasets().active()->name : QString();
    const QString m0 = themedMenuStyle(), d0 = themedDialogStyle();
    setActiveGammaset(themeName);
    const QString m1 = themedMenuStyle(), d1 = themedDialogStyle();
    // Dump the resolved wa_dlg colours under `themeName` — used to copy
    // a native theme's exact dialog palette into a synthetic one.
    for (const char *k : {"wasabi.window.background", "wasabi.window.text",
                          "wasabi.list.background", "wasabi.list.text",
                          "wasabi.list.text.selected.background",
                          "wasabi.list.text.selected",
                          "wasabi.button.text", "wasabi.button.dimmedText"}) {
        const QColor c = colors().resolve(QString::fromLatin1(k),
                                          &gammasets(), QColor());
        fprintf(stderr, "[wadlg] %-40s = %s\n", k,
                c.isValid() ? c.name().toLocal8Bit().constData()
                            : "(absent)");
    }
    setActiveGammaset(active0);   // restore
    auto firstColor = [](const QString &qss) {
        const int i = qss.indexOf(QStringLiteral("background-color:"));
        return i < 0 ? QString() : qss.mid(i + 17, 7);
    };
    fprintf(stderr, "[selftest] theme '%s'->'%s'  menu bg %s->%s  dialog bg %s->%s\n",
            active0.toLocal8Bit().constData(),
            themeName.toLocal8Bit().constData(),
            firstColor(m0).toLocal8Bit().constData(),
            firstColor(m1).toLocal8Bit().constData(),
            firstColor(d0).toLocal8Bit().constData(),
            firstColor(d1).toLocal8Bit().constData());
    fprintf(stderr, "[selftest] menu re-tint:   %s\n",
            m0 != m1 ? "PASS (differs)" : "FAIL (unchanged)");
    fprintf(stderr, "[selftest] dialog re-tint: %s\n",
            d0 != d1 ? "PASS (differs)" : "FAIL (unchanged)");
}

}  // namespace qtWasabi::head
