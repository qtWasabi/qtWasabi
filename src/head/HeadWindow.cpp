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
#include <qtWasabi/TreePainter.h>
#include <qtWasabi/SkinView.h>
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

QSize imageSizeForHitTest(const QString &bitmapId, void *userdata) {
    auto *registry = static_cast<qtWasabi::BitmapRegistry *>(userdata);
    if (!registry) return QSize();
    const auto *def = registry->find(bitmapId);
    // NStatesButton convention: when the bare image id isn't a
    // registered bitmap, fall back to `<id>0` (e.g. `repeat` →
    // `repeat0`).  Without this, NStates buttons with no explicit
    // w/h are invisible to hit-test and click-through never reaches
    // them.
    if (!def && !bitmapId.isEmpty()) {
        def = registry->find(bitmapId + QStringLiteral("0"));
    }
    if (!def) return QSize();
    if (!def->srcRect.isEmpty()) return def->srcRect.size();
    // Whole-image: load the image once and return its size.
    QImage img = registry->imageFor(*def);
    return img.size();
}

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
    // Any previously-open subwindows belong to the old skin doc.
    for (auto *w : std::as_const(m_subwindows)) if (w) w->deleteLater();
    m_subwindows.clear();
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

void HeadWindow::toggleSubwindow(const QString &containerRef) {
    if (!isMainRoot()) return;
    // `containerRef` is the skin's TOGGLE param — either a literal
    // container id, a component GUID, or one of Winamp's short
    // component aliases (`pl`, `ml`, `vid`, `vis`, …).  Resolve it
    // to the actual <container id="…"> via the engine so e.g.
    // `guid:pl` opens <container id="Pledit"> and `guid:ml` opens
    // <container id="MLibrary"> (Winamp Modern names its windows by
    // component GUID, not by the `pl`/`ml` strings the buttons fire).
    QString containerId =
        SkinXml::resolveContainerId(m_doc, containerRef);
    if (containerId.isEmpty()) {
        // Fall back to the raw reference so a direct-id skin (or a
        // future container we don't alias) still gets a chance, and
        // the load() failure path below logs a useful message.
        containerId = containerRef;
    }
    SkinView *slot = ensureSubwindow(containerRef);
    if (!slot) return;
    if (slot->isVisible()) slot->hide();
    else                   slot->show();
}

SkinView *HeadWindow::ensureSubwindow(const QString &containerRef) {
    // A single-container head renders exactly one window; TOGGLE
    // actions and Maki showWindow must not spawn siblings there.
    if (!isMainRoot()) return nullptr;
    QString containerId =
        SkinXml::resolveContainerId(m_doc, containerRef);
    if (containerId.isEmpty()) containerId = containerRef;
    SkinView *&slot = m_subwindows[containerId.toLower()];
    if (!slot) {
        slot = new SkinView();
        slot->setWindowTitle(subwindowTitle(containerId));
        slot->setWindowFlags(slot->windowFlags() | Qt::FramelessWindowHint);
        slot->setAttribute(Qt::WA_TranslucentBackground);
        slot->setHost(m_host);
        QString err;
        if (!slot->load(m_doc, containerId, QStringLiteral("normal"),
                        &err)) {
            fprintf(stderr,
                "[qtwasabi-head] failed to open container %s: %s\n",
                containerId.toLocal8Bit().constData(),
                err.toLocal8Bit().constData());
            slot->deleteLater();
            slot = nullptr;
            return nullptr;
        }
        // Tint the subwindow's chrome with the SAME active colour theme
        // as the main window — otherwise its titlebar/frame greyscale
        // bitmaps render untinted (the Winamp Modern titlebar gradient
        // is a tinted base, so no active gammaset = a blank/transparent
        // titlebar).
        if (const Gammaset *g = gammasets().active())
            slot->setActiveGammaset(g->name);
        slot->resize(slot->layoutNativeSize());
    }
    return slot;
}

SkinView *HeadWindow::peekSubwindow(const QString &containerRef) const {
    QString containerId =
        SkinXml::resolveContainerId(m_doc, containerRef);
    if (containerId.isEmpty()) containerId = containerRef;
    return m_subwindows.value(containerId.toLower(), nullptr);
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

// Override SkinQuickItem's paint hook so the app's colour-themes
// list state (selectedRow / topRow / out-bbox) threads into
// qtWasabi's TreePainter without the engine having to know about
// that state.  qtWasabi stays a pure renderer; per-skin state is
// a property of the qtamp consumer.  Called from updatePaintNode
// into a transparent QImage buffer that gets uploaded as a
// QSGSimpleTextureNode.
void HeadWindow::paintInto(QPainter *p, const QSize &canvas) {
    // Do NOT clip the chrome paint to the window region.  The
    // user explicitly wants the chrome bitmap painted fully
    // (rectangular) — the rounded corners come from setMask on
    // the QQuickWindow shaping the OS-level surface, not from
    // alpha-zero pixels in the QImage (Wayfire renders those as
    // white instead of desktop-transparent, producing the
    // staircase-of-white visual the user has been calling out).
    m_ctListRect = QRect();
    qtWasabi::TreePainter::paintTree(
        p, tree(), registry(), fonts(),
        canvas, host(), &gammasets(), &colors(),
        m_ctSelectedRow, m_ctTopRow,
        &m_ctListRect, &m_ctTopRow, m_visMode);
}


void HeadWindow::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::RightButton) {
        // Skin scripts get first refusal on right-clicks — real
        // Winamp routes the button events to the widget under the
        // cursor and only shows the default menu when nothing
        // consumed them.  wa2songtimer.m binds TimerTrigger.
        // onRightButtonUp to pop its own elapsed/remaining menu.
        // Down and Up are dispatched back-to-back here (the skin
        // popup opens a nested loop that swallows the real release
        // anyway); receiver-gated, so widgets without handlers
        // cost nothing and fall through to qtamp's menu.
        const QPoint rp = e->position().toPoint();
        int consumed = 0;
        const QList<const qtWasabi::Layout::ResolvedWidget *> rHits =
            alphaHitTestList(rp, /*actionOnly=*/false,
                             imageSizeForHitTest, &registry());
        for (const auto *w : rHits) {
            if (!w || w->id.isEmpty()) continue;
            consumed += qtWasabi::fireWidgetXYEventOn(
                w, L"onRightButtonDown", rp.x(), rp.y());
            consumed += qtWasabi::fireWidgetXYEventOn(
                w, L"onRightButtonUp", rp.x(), rp.y());
            consumed += qtWasabi::fireWidgetEvent(
                w->id, L"onRightClick");
            if (consumed > 0) break;
        }
        if (consumed > 0) { update(); return; }
        // Right-click anywhere → Winamp-style context menu,
        // built against qtamp's qtWasabi::Host transport surface.
        showContextMenu(e->globalPosition().toPoint());
        return;
    }
    if (e->button() == Qt::LeftButton) {
        const QPoint p = e->position().toPoint();

        // (The old hardcoded drawer-toggle that intercepted clicks
        // at the fixed drawer.button position used to live here.
        // It bypassed Maki dispatch — clicking it ran qtamp's own
        // setDrawerOpen which forced drawer.button.open visible=0
        // permanently, breaking configtabs.maki's btnOpen.show()
        // path on the next close.  Removed: clicks now fall
        // through to the hit-test + fireWidgetEvent path so
        // configtabs.m's btnOpen.onLeftClick / btnClose.onLeftClick
        // handlers run normally.)

        QRect hitBbox;
        const auto *hit = qtWasabi::Layout::hitTest(
            tree(), p, /*actionOnly=*/true,
            imageSizeForHitTest, &registry(), &hitBbox);
        if (hit) {
            // Buttons / togglebuttons → action dispatch.
            // Sliders are intentionally NOT handled here — they
            // flow through the alphaHitTestList button-claim loop
            // below so SliderWidget's own onLeftButtonDown
            // (which handles vertical sliders, lastCanvasRect-
            // relative drag, and host-setSliderPosition writes)
            // owns the entire drag lifecycle.
            // Show the pressed-state bitmap (downImage) so an action
            // button visibly depresses.  This fast-path dispatches the
            // action and returns, otherwise skipping the press
            // lifecycle that script-driven buttons get via the
            // alphaHitTestList claim loop below — leaving transport
            // buttons looking dead even though they fire.  Plain
            // <button> only: togglebuttons cycle their state on
            // release and own their own press handling.  m_activeWidget
            // routes the matching onLeftButtonUp from mouseReleaseEvent.
            if (hit->tag == QLatin1String("button")) {
                auto *bw = const_cast<qtWasabi::Widget *>(hit);
                qtWasabi::PaintCtx bctx{};
                bctx.bmp  = &registry();
                bctx.host = host();
                bw->onLeftButtonDown(p, bctx);
                setActiveWidget(bw);
            }
            const QString action =
                hit->attrs.value(QStringLiteral("action"));
            fprintf(stderr, "[qtamp] action: %s\n",
                    action.toUpper().toLocal8Bit().constData());
            // `action="TOGGLE" param="<container-id>"` opens (or
            // toggles) a secondary container window from the same
            // skin doc — EQ, Playlist, etc.
            if (action.compare(QStringLiteral("TOGGLE"),
                               Qt::CaseInsensitive) == 0) {
                const QString param = hit->attrs.value(
                    QStringLiteral("param"));
                if (!param.isEmpty()) {
                    // Pass the raw param through — toggleSubwindow
                    // resolves "guid:pl" / "guid:ml" / a literal
                    // GUID / a literal container id to the actual
                    // <container id> via the engine's
                    // SkinXml::resolveContainerId().
                    toggleSubwindow(param);
                    return;
                }
            }
            // Embedder-owned actions (vis overlay prev/next, ...)
            // get first refusal before the generic dispatch.
            if (interceptAction(action,
                                hit->attrs.value(QStringLiteral("param"))))
                return;
            // EQ_TOGGLE used to be handled here as a special
            // action that flipped QtampHost::m_eqEnabled.  That
            // path returned early and prevented the button-
            // claim loop from running cycleOnRelease, so the
            // LEDs and the songinfo "eq" badge never lit up.
            // It's gone now: the EQ ON button has action=
            // EQ_TOGGLE + activeImage, so ButtonWidget gives it
            // a synthetic __action:EQ_TOGGLE cfgattrib; clicks
            // flow through the button-claim path to
            // cycleOnRelease which writes the store; the
            // QtampHost subscription updates m_eqEnabled, and
            // all three EQ indicators (button, drawer LED,
            // songinfo badge) read the same store and update in
            // lockstep.  Single source of truth.
            // dispatchAction wants a QWidget* embedder for file
            // dialogs; QQuickItem isn't a QWidget so pass nullptr.
            if (qtWasabi::dispatchAction(action, m_host, nullptr))
                return;
            // `action="X" action_target="Y"` — Wasabi's action-
            // dispatch protocol.  Scripts that own widget Y
            // implement `Y.onAction(action, param, x, y, p1, p2,
            // source)` and pivot on the action string (e.g.
            // configtarget.m's `target.onAction("switchto;…")`
            // swapping option pages).  Without this dispatch the
            // option-bucket buttons fire onLeftClick on themselves
            // but nothing happens because the actual logic lives
            // on the target widget.
            // `action="cb_prevpage|cb_nextpage" cbtarget="..."` —
            // componentbucket scroll arrows.  Look up the bucket
            // (we pick the only componentbucket in the parent
            // chain, since the `cbtarget="bucket"` alias doesn't
            // necessarily match the bucket's actual id) and bump
            // its `_scroll` attr.  TreePainter then translates the
            // bucket's children by -scroll * entry_step on each
            // paint so a different window of entries becomes visible.
            if (action.compare(QStringLiteral("cb_prevpage"),
                               Qt::CaseInsensitive) == 0 ||
                action.compare(QStringLiteral("cb_nextpage"),
                               Qt::CaseInsensitive) == 0) {
                const QString cbtarget = hit->attrs.value(
                    QStringLiteral("cbtarget"));
                auto &mut = const_cast<qtWasabi::Layout::ResolvedWidget &>(
                    tree());
                std::function<qtWasabi::Layout::ResolvedWidget *(
                    qtWasabi::Layout::ResolvedWidget &)> findBucket =
                    [&](qtWasabi::Layout::ResolvedWidget &w)
                          -> qtWasabi::Layout::ResolvedWidget * {
                    if (w.tag == QStringLiteral("componentbucket")) {
                        if (cbtarget.isEmpty() ||
                            w.id.compare(cbtarget,
                                          Qt::CaseInsensitive) == 0 ||
                            w.id.endsWith(
                                QChar('.') + cbtarget,
                                Qt::CaseInsensitive))
                            return &w;
                    }
                    for (auto &c : w.children)
                        if (c) if (auto *r = findBucket(*c)) return r;
                    return nullptr;
                };
                auto *buck = findBucket(mut);
                if (buck) {
                    int sc = buck->attrs.value(
                        QStringLiteral("_scroll")).toInt();
                    const int cnt = buck->attrs.value(
                        QStringLiteral("_entry_count")).toInt();
                    // Page size derived from bucket.h / entry_step:
                    const int step = qMax(1, buck->attrs.value(
                        QStringLiteral("_entry_step")).toInt());
                    const int viewport = buck->attrs.value(
                        QStringLiteral("h")).toInt() / step;
                    const int maxScroll = qMax(0, cnt - viewport);
                    sc += (action.compare(
                        QStringLiteral("cb_nextpage"),
                        Qt::CaseInsensitive) == 0) ? 1 : -1;
                    sc = qBound(0, sc, maxScroll);
                    // Route through setXmlParam so
                    // ComponentBucketWidget can shadow the value
                    // on its typed state member.
                    buck->setXmlParam(QStringLiteral("_scroll"),
                                       QString::number(sc));
                    update();
                    return;
                }
            }
            if (!action.isEmpty()) {
                const QString target = hit->attrs.value(
                    QStringLiteral("action_target"));
                if (!target.isEmpty()) {
                    // Handle the universal switchto;GROUPID
                    // protocol directly so the option pages work
                    // regardless of whether the script-side
                    // onAction dispatch reads its args correctly.
                    // Wasabi's contract is the same in real
                    // Winamp: a button with
                    // action="switchto;<groupdef-id>" populates
                    // its action_target widget with that groupdef.
                    if (action.startsWith(
                            QStringLiteral("switchto;"),
                            Qt::CaseInsensitive)) {
                        const QString grp =
                            action.section(QChar(';'), 1, 1);
                        if (!grp.isEmpty()) {
                            qtWasabi::fireWidgetAttrSet(
                                target, QStringLiteral("groupid"), grp);
                            update();
                            return;
                        }
                    }
                    const QString param = hit->attrs.value(
                        QStringLiteral("param"));
                    int fired = qtWasabi::fireWidgetActionEvent(
                        target, action, param,
                        p.x(), p.y(), 0, 0, hit->id);
                    if (fired > 0) {
                        update();
                        return;
                    }
                }
            }
            // Universal Maki click dispatch: when no built-in
            // action fired, broadcast onLeftClick to any handler
            // that bound to this widget id.  This is how skin
            // scripts wire their own button behaviour (e.g.
            // videoavs.m's btnOpen.onLeftClick → openDrawer())
            // without us needing per-skin glue.
            if (!hit->id.isEmpty()) {
                int fired = qtWasabi::fireWidgetEvent(
                    hit->id, L"onLeftClick");
                if (fired > 0) {
                    update();
                    return;
                }
            }
        }
        // Alpha-aware hit-test list: every widget at the click
        // point that's opaque in the composite alpha, ordered
        // topmost-first.  Wasabi's event model bubbles unhandled
        // clicks down the z-order, so we iterate: the first widget
        // whose Maki handler dispatches (fired > 0) consumes the
        // click.  Chrome layers without handlers are skipped over
        // — they're opaque pixels with no script binding, so the
        // click should reach the button visually behind them.
        const QList<const qtWasabi::Layout::ResolvedWidget *> hits =
            alphaHitTestList(p, /*actionOnly=*/false,
                              imageSizeForHitTest, &registry());
        const qtWasabi::Layout::ResolvedWidget *hit2 = nullptr;
        // For button-family widgets, fire onLeftButtonDown on the
        // topmost interactive hit BEFORE Maki dispatch so the
        // pressed-state bitmap shows immediately.  The widget is
        // remembered in m_activeWidget so mouseReleaseEvent can
        // route the matching onLeftButtonUp (clearing m_pressed).
        // Menu is handled as a special case below — it short-
        // circuits because its onLeftButtonDown is meant to claim
        // the click entirely (popup spawn, no Maki dispatch).
        bool buttonClaimed = false;
        for (const auto *w : hits) {
            if (!w) continue;
            // Capture-style widgets take the press for the whole
            // press→move→release lifecycle: button/slider families by
            // tag, plus any widget that reports capturesMouse() — the
            // playlist / library list holders, whose onLeftButtonDown
            // selects the row under the cursor and arms scrollbar drag.
            // Claiming here (buttonClaimed) is also what stops the
            // press from falling through to the window-move drag.
            if (w->tag == QLatin1String("button") ||
                w->tag == QLatin1String("togglebutton") ||
                w->tag == QLatin1String("nstatesbutton") ||
                w->tag == QLatin1String("slider") ||
                w->capturesMouse()) {
                auto *bw = const_cast<qtWasabi::Widget *>(w);
                qtWasabi::PaintCtx bctx{};
                bctx.bmp  = &registry();
                bctx.host = host();
                bw->onLeftButtonDown(p, bctx);
                setActiveWidget(bw);
                buttonClaimed = true;
                break;
            }
        }
        for (const auto *w : hits) {
            if (!w || w->id.isEmpty()) continue;
            // Compiled widget behaviours first — these widgets have
            // built-in onLeftButtonDown handlers (Menu's hover/
            // down state swap, Slider drag init, ScrollBar thumb
            // grab) that take precedence over Maki onLeftClick.
            // Dispatch through the Widget virtual; if it claims
            // the click we remember the widget so mouseRelease
            // can route the corresponding onLeftButtonUp.
            if (w->tag == QLatin1String("menu")) {
                qtWasabi::PaintCtx mctx{};
                const QString firstId =
                    w->attrs.value(QStringLiteral("menu"));
                if (firstId.isEmpty()) {
                    // No menu= target: engine down-state toggle only.
                    auto *mw = const_cast<qtWasabi::Widget *>(w);
                    mw->onLeftButtonDown(p, mctx);
                    setActiveWidget(mw);
                    update();
                    return;
                }
                // Open the popup, showing the down face while it's up.
                // openMenuBarMenu returns the next menu widget to chain to
                // when the cursor swept onto a sibling in the same
                // menugroup — loop until a menu closes normally.
                const qtWasabi::Widget *cur = w;
                while (cur) {
                    const QString menuId =
                        cur->attrs.value(QStringLiteral("menu"));
                    if (menuId.isEmpty()) break;
                    const QString wid = cur->id;
                    auto *mw = const_cast<qtWasabi::Widget *>(cur);
                    mw->onLeftButtonDown(p, mctx);   // → down
                    update();
                    // Anchor at the menu button's bottom-left.
                    QPoint anchor =
                        mapToGlobal(QPointF(p.x(), p.y())).toPoint();
                    const QRect cr = cur->lastCanvasRect;
                    if (cr.isValid())
                        anchor = mapToGlobal(QPointF(
                            cr.x(), cr.y() + cr.height())).toPoint();
                    const qtWasabi::Widget *next =
                        openMenuBarMenu(menuId, anchor, cur);
                    // A menu action (e.g. switching skins via
                    // Options > Preferences) can rebuild the whole
                    // widget tree, freeing mw/next.  Only touch a
                    // pointer still registered as the live widget for
                    // its id; otherwise the tree was replaced and these
                    // pointers dangle — stop, the fresh tree is normal.
                    const bool stillLive = !wid.isEmpty() &&
                        qtWasabi::Widget::findById(wid) == mw;
                    if (stillLive) {
                        mw->onLeftButtonDown(p, mctx);   // → normal
                        cur = next;
                    } else {
                        cur = nullptr;
                    }
                    update();
                }
                setActiveWidget(nullptr);
                return;
            }
            // Standard window-control buttons (maximize/restore in
            // the shared standardframe) carry no action= attribute;
            // the skin's own simplemaximize.maki onLeftClick drives
            // them through the Maki resize() binding, so no per-skin
            // interception is needed here — the click falls through
            // to the generic onLeftClick dispatch below.
            applyDrawerModeFixup(w->id);
            // GuiObject press event first — scripts that bind
            // onLeftButtonDown(x, y) (wa2songtimer.m's elapsed/
            // remaining toggle on the invisible TimerTrigger
            // layer) get real button semantics; the widget id is
            // remembered so mouseReleaseEvent routes the matching
            // onLeftButtonUp.  onLeftClick stays the second try
            // for the (far more common) click-bound handlers.
            const int downFired = qtWasabi::fireWidgetXYEventOn(
                w, L"onLeftButtonDown", p.x(), p.y());
            if (downFired > 0) { m_makiPressId = w->id; m_makiPressWidget = w; }
            int fired = qtWasabi::fireWidgetEvent(
                w->id, L"onLeftClick");
            if (::getenv("WASABIQT_TRACE_MAKI"))
                fprintf(stderr,
                    "[click] (%d,%d) alpha hit id=%s fired=%d down=%d\n",
                    p.x(), p.y(),
                    w->id.toLocal8Bit().constData(), fired, downFired);
            if (fired > 0 || downFired > 0) {
                update();
                return;
            }
            // Event bubbling: a click on a nested widget whose own id has
            // no onLeftClick handler (a tab's `bento.tabbutton.mousetrap`)
            // should reach a handler bound on an ENCLOSING group — the
            // Maki tab flow binds `switch.X.onLeftClick` and the click
            // lands on the inner mousetrap.  Walk up the parent chain and
            // fire onLeftClick on each id'd ancestor; receiver-gated, so
            // passive ancestors cost nothing.  This is what drives
            // suicore's switchToX (replacing the removed wireTabs system).
            for (const qtWasabi::Widget *a = w->parentWidget;
                 a; a = a->parentWidget) {
                if (a->id.isEmpty()) continue;
                if (qtWasabi::fireWidgetEvent(a->id, L"onLeftClick") > 0) {
                    update();
                    return;
                }
            }
            // First widget with an id becomes the tab-switcher
            // candidate even if it had no Maki handler.
            if (!hit2) hit2 = w;
        }
        if (hit2) {
            // The `.off` tab variants ship with a top-level
            // `mousetrapTab*` layer, but the `.on` variants
            // don't — clicking the active tab lands on its
            // text label or Grid bg instead.  Match the tab
            // by id pattern so any widget inside a tab group
            // (mousetrap, label, or grid) counts as a click
            // on that tab.
            const QString id = hit2->id;
            int newTab = 0;
            if (id.contains(QLatin1String("TabEQ")) ||
                id.contains(QLatin1String("eq.on")) ||
                id.contains(QLatin1String("eq.off")))           newTab = 1;
            else if (id.contains(QLatin1String("TabOPTIONS")) ||
                     id.contains(QLatin1String("options.on")) ||
                     id.contains(QLatin1String("options.off"))) newTab = 2;
            else if (id.contains(QLatin1String("TabCOLORTHEMES")) ||
                     id.contains(QLatin1String("colorthemes.on")) ||
                     id.contains(QLatin1String("colorthemes.off"))) newTab = 3;
            if (newTab != 0) {
                switchDrawerTab(newTab);
                update();
                return;
            }
        }
        // ColorThemes list — clicking a row selects it; the
        // Switch button (action=colorthemes_switch) then
        // applies the chosen gammaset.  Hit-test the bbox the
        // painter cached.  Also detect clicks in the
        // scrollbar column on the right edge.
        const QRect ctRect = colorThemesListRect();
        if (ctRect.isValid()) {
            if (ctRect.contains(p)) {
                const int rowH = 10;
                const int row = (p.y() - ctRect.y() - 2) / rowH;
                if (row >= 0) {
                    setColorThemesSelectedRow(
                        colorThemesTopRow() + row);
                    return;
                }
            }
            // Scrollbar column sits in the 14 px to the right
            // of the list rect (the painter reserves it).  The
            // top 17 px is the up-arrow, the bottom 17 px is
            // the down-arrow, and the 31 px thumb floats in
            // the middle (drag to scroll).
            const QRect sb(ctRect.right() + 1, ctRect.y(),
                            14, ctRect.height());
            if (sb.contains(p)) {
                const int arrowH = 17;
                const int thumbH = 31;
                // Top arrow: scroll up one row.
                if (p.y() < sb.y() + arrowH) {
                    setColorThemesTopRow(qMax(0,
                        colorThemesTopRow() - 1));
                    return;
                }
                // Bottom arrow: scroll down one row.
                if (p.y() >= sb.y() + sb.height() - arrowH) {
                    setColorThemesTopRow(
                        colorThemesTopRow() + 1);
                    return;
                }
                // Middle area: capture the start of a thumb
                // drag.  Compute the thumb's current rect so
                // the click point can be inside or outside it
                // (page jump on outside).
                const int trackTop = sb.y() + arrowH;
                const int trackBot = sb.y() + sb.height() - arrowH;
                const int travel   = qMax(0, (trackBot - trackTop) - thumbH);
                int nrows = gammasets().names().size();
                const int maxTop = qMax(0, nrows - 8);
                const double frac = maxTop > 0
                    ? double(colorThemesTopRow()) / double(maxTop)
                    : 0.0;
                const int thumbY = trackTop + int(frac * travel);
                if (p.y() >= thumbY && p.y() < thumbY + thumbH) {
                    // Start drag tracking — store the y offset
                    // from thumb's top so subsequent moves
                    // keep the cursor aligned.
                    m_ctDragging  = true;
                    m_ctDragOffset = p.y() - thumbY;
                    m_ctTrackTop  = trackTop;
                    m_ctTrackBot  = trackBot;
                    m_ctThumbH    = thumbH;
                    m_ctMaxTop    = maxTop;
                    return;
                }
                // Click in the empty track: page up/down.
                if (p.y() < thumbY)
                    setColorThemesTopRow(qMax(0,
                        colorThemesTopRow() - 4));
                else
                    setColorThemesTopRow(
                        colorThemesTopRow() + 4);
                return;
            }
        }
        // Colour-themes drawer actions — qtamp-specific
        // (Audacious and other Wasabi 2-style hosts simply
        // wouldn't bind handlers for these).  Three buttons
        // ship in the skin XML as:
        //   action="colorthemes_switch"  - apply selected
        //   action="colorthemes_previous" - select prev row
        //   action="colorthemes_next"     - select next row
        if (hit2) {
            const QString a = hit2->attrs.value(QStringLiteral("action")).toLower();
            QStringList names = gammasets().names();
            std::sort(names.begin(), names.end(),
                      [](const QString &x, const QString &y){
                          return x.compare(y, Qt::CaseInsensitive) < 0;
                      });
            int row = colorThemesSelectedRow();
            if (row < 0 || row >= names.size()) {
                // Resolve "no explicit selection" to the active
                // gammaset's row so prev/next have something to
                // start from.
                const auto *act = gammasets().active();
                const QString actName = act ? act->name : QString();
                row = qMax(0, names.indexOf(actName));
            }
            if (a == QLatin1String("colorthemes_switch")) {
                if (row >= 0 && row < names.size()) {
                    setActiveGammaset(names[row]);
                    // Remember the choice (real Winamp persists the
                    // colour theme across restarts).
                    if (!settingsFile().isEmpty())
                        QSettings(settingsFile(), QSettings::IniFormat)
                            .setValue(QStringLiteral("player/colortheme"),
                                      names[row]);
                }
                return;
            }
            if (a == QLatin1String("colorthemes_previous")) {
                setColorThemesSelectedRow(qMax(0, row - 1));
                return;
            }
            if (a == QLatin1String("colorthemes_next")) {
                setColorThemesSelectedRow(
                    qMin(names.size() - 1, row + 1));
                return;
            }
        }
        // Empty-area click — start a window drag.  Skip when a
        // button widget already claimed the press: the button's
        // mouseReleaseEvent path needs to see the matching
        // release to fire onLeftButtonUp (and let togglebutton /
        // nstatesbutton cycle their state).  Without this skip,
        // clicks on auto-cycling state widgets (Repeat / Shuffle
        // / Random — none of them have a Maki onLeftClick
        // handler that would fire>0 above) fell through to
        // startSystemMove and the window dragged instead.
        if (buttonClaimed) {
            update();
            return;
        }
        // Empty area near an edge/corner → resize (native or manual
        // fallback) BEFORE window-move, so dragging the border resizes
        // instead of moving.  This must precede startSystemMove because
        // on Wayland startSystemMove always succeeds and would win.
        if (beginEdgeResize(e->position(), e->globalPosition().toPoint())) {
            e->accept();
            return;
        }
        if (::getenv("WASABIQT_TRACE_MAKI"))
            fprintf(stderr,
                "[click] (%d,%d) falling through to window drag "
                "(hit=%s hit2=%s)\n",
                p.x(), p.y(),
                hit ? hit->id.toLocal8Bit().constData() : "(null)",
                hit2 ? hit2->id.toLocal8Bit().constData() : "(null)");
        if (window() && window()->startSystemMove())
            return;
        m_dragOrigin = e->globalPosition().toPoint() -
                       (window() ? window()->position() : QPoint(0,0));
        m_dragging = true;
    }
    qtWasabi::SkinQuickItem::mousePressEvent(e);
}


// m_activeWidget can be freed under us if the widget tree is rebuilt
// (theme/skin reload, Maki relayout) between press and release.  Detect
// that by id — a pointer compare against the live registry, never a
// deref of the (possibly freed) widget — and drop the stale pointer.
void HeadWindow::setActiveWidget(qtWasabi::Widget *w) {
    m_activeWidget   = w;
    m_activeWidgetId = w ? w->id : QString();
}
bool HeadWindow::activeWidgetStale() const {
    return m_activeWidget && !m_activeWidgetId.isEmpty() &&
           qtWasabi::Widget::findById(m_activeWidgetId) != m_activeWidget;
}


void HeadWindow::mouseMoveEvent(QMouseEvent *e) {
    if (activeWidgetStale()) setActiveWidget(nullptr);
    // Slider / list-holder drag — m_activeWidget owns the press,
    // forward every move to it until release.  Slider's onMouseMove
    // updates the host position; a playlist/library holder's
    // onMouseMove drags its scrollbar thumb (capturesMouse() covers
    // those — without this the scrollbar couldn't be dragged).
    if (m_activeWidget && (e->buttons() & Qt::LeftButton) &&
        (m_activeWidget->tag == QLatin1String("slider") ||
         m_activeWidget->capturesMouse())) {
        qtWasabi::PaintCtx ctx{};
        ctx.bmp  = &registry();
        ctx.host = host();
        m_activeWidget->onMouseMove(e->position().toPoint(), ctx);
        update();
        return;
    }
    // (Legacy m_sliderAction drag removed — SliderWidget's own
    // onMouseMove handles drag now, via the m_activeWidget block
    // above.  The legacy path only handled horizontal sliders
    // and used a wrong coord system for vertical EQ bands.)
    if (m_ctDragging && (e->buttons() & Qt::LeftButton)) {
        // Map cursor y to thumb top, then to a row fraction.
        const int y = e->position().toPoint().y();
        const int wantThumbY = y - m_ctDragOffset;
        const int travel = qMax(0,
            (m_ctTrackBot - m_ctTrackTop) - m_ctThumbH);
        const double frac = travel > 0
            ? qBound(0.0,
                double(wantThumbY - m_ctTrackTop) / double(travel),
                1.0)
            : 0.0;
        setColorThemesTopRow(int(frac * m_ctMaxTop + 0.5));
        return;
    }
    if (m_dragging && (e->buttons() & Qt::LeftButton) && window()) {
        window()->setPosition(e->globalPosition().toPoint() - m_dragOrigin);
    }
    qtWasabi::SkinQuickItem::mouseMoveEvent(e);
}


void HeadWindow::mouseReleaseEvent(QMouseEvent *e) {
    m_dragging = false;
    m_ctDragging = false;
    // Route the matching GuiObject onLeftButtonUp to the script
    // receiver whose onLeftButtonDown claimed the press.  Guarded
    // by findById: a Down handler can rebuild the tree (skin
    // switch), freeing the remembered pointer.
    if (!m_makiPressId.isEmpty() && e->button() == Qt::LeftButton) {
        const QPoint rp = e->position().toPoint();
        if (m_makiPressWidget &&
            qtWasabi::Widget::findById(m_makiPressId) ==
                m_makiPressWidget) {
            qtWasabi::fireWidgetXYEventOn(
                m_makiPressWidget, L"onLeftButtonUp", rp.x(), rp.y());
        }
        m_makiPressId.clear();
        m_makiPressWidget = nullptr;
        update();
    }
    if (activeWidgetStale()) setActiveWidget(nullptr);
    if (m_activeWidget && e->button() == Qt::LeftButton) {
        qtWasabi::PaintCtx mctx{};
        mctx.bmp  = &registry();
        mctx.host = host();
        m_activeWidget->onLeftButtonUp(
            e->position().toPoint(), mctx);
        setActiveWidget(nullptr);
        update();
    }
    qtWasabi::SkinQuickItem::mouseReleaseEvent(e);
}


void HeadWindow::keyPressEvent(QKeyEvent *e) {
    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    if (e->key() == Qt::Key_Escape) {
        if (window()) window()->close();
        return;
    }
    if (ctrl && (e->key() == Qt::Key_O || e->key() == Qt::Key_L)) {
        // pickFile takes a QWidget* for the file-dialog parent.
        // Pass nullptr so the dialog parents to QGuiApplication;
        // the QQuickWindow itself isn't a QWidget.
        m_host->pickFile(nullptr);   // enqueues + plays internally
        return;
    }
    if (e->key() == Qt::Key_Space) {
        m_host->isPlaying() ? m_host->pause() : m_host->play();
        return;
    }
    if (e->key() == Qt::Key_MediaPlay)  { m_host->play();  return; }
    if (e->key() == Qt::Key_MediaPause) { m_host->pause(); return; }
    if (e->key() == Qt::Key_MediaStop)  { m_host->stop();  return; }
    // Up/Down arrows scroll the colour-themes list when its
    // tab is open.  Works regardless of wheel-event delivery
    // (frameless + translucent backgrounds sometimes swallow
    // wheel events on Wayland).
    if (e->key() == Qt::Key_Down) {
        setColorThemesTopRow(colorThemesTopRow() + 1);
        return;
    }
    if (e->key() == Qt::Key_Up) {
        setColorThemesTopRow(qMax(0, colorThemesTopRow() - 1));
        return;
    }
    if (e->key() == Qt::Key_PageDown) {
        setColorThemesTopRow(colorThemesTopRow() + 5);
        return;
    }
    if (e->key() == Qt::Key_PageUp) {
        setColorThemesTopRow(qMax(0, colorThemesTopRow() - 5));
        return;
    }
    qtWasabi::SkinQuickItem::keyPressEvent(e);
}


void HeadWindow::wheelEvent(QWheelEvent *e) {
    // Wheel scroll inside the colour-themes list moves the
    // top-row offset.  Outside the list area, fall through.
    const QPoint p = e->position().toPoint();
    const QRect lr = colorThemesListRect();
    if (::getenv("WASABIQT_TRACE_MAKI")) {
        fprintf(stderr, "[wheel] at (%d,%d) ct_rect=%dx%d+%d+%d valid=%d "
                "contains=%d delta=%d\n",
                p.x(), p.y(),
                lr.width(), lr.height(), lr.x(), lr.y(),
                lr.isValid()?1:0, lr.contains(p)?1:0,
                e->angleDelta().y());
        fflush(stderr);
    }
    if (lr.isValid() && lr.contains(p)) {
        const int steps = e->angleDelta().y() / 120;  // 1 notch = 120
        setColorThemesTopRow(qMax(0,
            colorThemesTopRow() - steps));
        return;
    }
    qtWasabi::SkinQuickItem::wheelEvent(e);
}


// Perform a widget's `action=` exactly as a real click would.  The engine
// calls this (registerSkinWidgetClickCallback) from Maki's
// GuiObject.leftClick()/rightClick() when a script delegates a click and
// the target didn't consume it with an onLeftClick/onRightClick handler —
// so scripted click-delegation drives transport/toggle/page buttons on ANY
// skin, not just ones whose buttons carry a script handler.  Mirrors this
// window's real mousePressEvent dispatch (TOGGLE, builtin verbs,
// action_target) minus the hit-test/press-visual, since the target widget
// is named directly (and may be hidden, which a point hit-test can't reach).
bool HeadWindow::triggerWidgetActionById(const QString &id,
                                         bool /*right*/) {
    const qtWasabi::Layout::ResolvedWidget *w = nullptr;
    std::function<void(const qtWasabi::Layout::ResolvedWidget &)> find =
        [&](const qtWasabi::Layout::ResolvedWidget &n) {
            if (w) return;
            if (n.id.compare(id, Qt::CaseInsensitive) == 0) { w = &n; return; }
            for (const auto &c : n.children) if (c) find(*c);
        };
    find(tree());
    if (!w) return false;
    const QString action = w->attrs.value(QStringLiteral("action"));
    if (action.isEmpty()) return false;
    if (qEnvironmentVariableIntValue("WASABIQT_TRACE_MAKI") == 1)
        fprintf(stderr, "[clickaction] %s -> action=%s\n",
                w->id.toLocal8Bit().constData(),
                action.toLocal8Bit().constData());
    if (action.compare(QStringLiteral("TOGGLE"), Qt::CaseInsensitive) == 0) {
        const QString param = w->attrs.value(QStringLiteral("param"));
        if (!param.isEmpty()) { toggleSubwindow(param); return true; }
    }
    if (qtWasabi::dispatchAction(action, m_host, nullptr)) return true;
    const QString target = w->attrs.value(QStringLiteral("action_target"));
    if (!target.isEmpty()) {
        const QString param = w->attrs.value(QStringLiteral("param"));
        if (action.startsWith(QStringLiteral("switchto;"),
                              Qt::CaseInsensitive)) {
            const QString grp = action.section(QChar(';'), 1, 1);
            if (!grp.isEmpty()) {
                qtWasabi::fireWidgetAttrSet(
                    target, QStringLiteral("groupid"), grp);
                update();
                return true;
            }
        }
        if (qtWasabi::fireWidgetActionEvent(
                target, action, param, 0, 0, 0, 0, w->id) > 0) {
            update();
            return true;
        }
    }
    return false;
}


// AVS-drawer mode-switch fixup.  The Maki chain in qtwasabi
// doesn't propagate drawer_showVideo's getVideoWindowHolder()
// .show() through to the WindowHolder widget — onShowVideo()'s
// button-bar swap (vis ↔ video) fires fine but the holder
// visibility never flips.  Recognise the two switchto clicks by
// widget id and toggle myviswnd/myvideownd visibility here in
// lockstep with the Maki dispatch.  By the Wasabi model this is
// handled entirely in script; this hook is a pragmatic interim
// until the VM correctly caches the holder lookup.  Case-tolerant
// because skin XML uses lowercase ids while Maki globals are
// bound with CamelCase aliases.
void HeadWindow::applyDrawerModeFixup(const QString &clickedId) {
    auto setVis = [](const char *id, bool on) {
        if (auto *wnd = qtWasabi::Widget::findById(
                QString::fromLatin1(id)))
            wnd->setXmlParam(QStringLiteral("visible"),
                on ? QStringLiteral("1") : QStringLiteral("0"));
    };
    // Per-mode visibility set covering BOTH the render slots
    // (myviswnd / myvideownd) and the corresponding button bars
    // (buttons.vis* / buttons.video*).  Wasabi's onShowVis /
    // onShowVideo drives all of these in script, but our Timer
    // port refuses the script's hides at SkinRuntimeBridge (the
    // Timer-gated re-show would otherwise leave them invisible
    // forever).  We bypass that by setting attrs directly here.
    auto applyMode = [&](bool vis) {
        // Render slot.
        setVis("myviswnd",                vis);
        setVis("myvideownd",             !vis);
        // Button bar — "Switch" + "Detach" labels + the parent
        // container groups that hold the secondary controls
        // (Prev/Next/Random for vis, 1x/2x/FS for video).
        setVis("buttons.vis",             vis);
        setVis("buttons.vis.detach",      vis);
        setVis("buttons.vis.switchto",    vis);
        setVis("buttons.video",          !vis);
        setVis("buttons.video.detach",   !vis);
        setVis("buttons.video.switchto", !vis);
    };
    if (clickedId.compare(QLatin1String("button.vis.switchto"),
                           Qt::CaseInsensitive) == 0) {
        applyMode(false);   // switch to video mode
    } else if (clickedId.compare(QLatin1String("button.vid.switchto"),
                                  Qt::CaseInsensitive) == 0) {
        applyMode(true);    // switch to visualiser mode
    } else if (clickedId.compare(QLatin1String("videoavs.open"),
                                  Qt::CaseInsensitive) == 0) {
        // Drawer open — initial mode is visualiser.  Without
        // this, myviswnd stays visible="0" (XML default), the
        // milkdrop placeholder inside it never paints, and the
        // MilkdropItem GL overlay never wakes up.  User would
        // see a black drawer until they ping-pong through video
        // mode and back.  Mirrors the Timer-driven Maki
        // drawer_showVis path that our partial Timer port skips.
        applyMode(true);
    }
}


// Mirror configtabs.m::setTabs(int): show the .on variant of
// the selected tab + its content page, hide the others.  Only
// touches `visible` attrs inside the drawer — the drawer's own
// sysregion-bearing widgets are untouched, so the window
// region clip stays in sync without needing a rebuild.
// Mirror configtabs.m's OpenDrawer / closeDrawer.  When closed
// the drawer slides up to y=17 so it sits behind player.main
// (out of view); the drawer.button.open is then the only
// child still visible — it pokes through the player chrome's
// CONFIG notch.  When open the drawer sits at y=133 (below
// player.main, the position we already use as the default).
void HeadWindow::setDrawerOpen(bool open) {
    if (open == m_drawerOpen) return;
    m_drawerOpen = open;
    auto &mut = const_cast<qtWasabi::Layout::ResolvedWidget &>(tree());
    std::function<void(qtWasabi::Layout::ResolvedWidget &)> walk =
        [&](qtWasabi::Layout::ResolvedWidget &w) {
        if (w.id == QStringLiteral("player.normal.drawer")) {
            w.attrs.insert(QStringLiteral("y"),
                open ? QStringLiteral("133") : QStringLiteral("17"));
            w.attrs.remove(QStringLiteral("relaty"));
        } else if (w.id == QStringLiteral("player.normal.drawer.shadow")) {
            w.attrs.insert(QStringLiteral("visible"),
                open ? QStringLiteral("1") : QStringLiteral("0"));
            w.attrs.insert(QStringLiteral("y"),
                open ? QStringLiteral("121") : QStringLiteral("0"));
        } else if (w.id == QStringLiteral("player.normal.drawer.content")) {
            // The chrome/inner-borders/list live inside content;
            // hide it when closed so only the open-button area
            // (rendered later in the tree) shows through.
            w.attrs.insert(QStringLiteral("visible"),
                open ? QStringLiteral("1") : QStringLiteral("0"));
        } else if (w.id == QStringLiteral("drawer.button.close")) {
            // Single toggle button: always visible, image swapped
            // to flip the arrow.  When the drawer is open it shows
            // the up-arrow ("close"); when closed it shows the
            // down-arrow ("open").
            w.attrs.insert(QStringLiteral("visible"),
                QStringLiteral("1"));
            w.attrs.insert(QStringLiteral("image"),
                open ? QStringLiteral("drawer.button.close")
                     : QStringLiteral("drawer.button.open"));
            w.attrs.insert(QStringLiteral("hoverImage"),
                open ? QStringLiteral("drawer.button.close.hover")
                     : QStringLiteral("drawer.button.open.hover"));
            w.attrs.insert(QStringLiteral("downImage"),
                open ? QStringLiteral("drawer.button.close.pressed")
                     : QStringLiteral("drawer.button.open.pressed"));
        } else if (w.id == QStringLiteral("drawer.button.open")) {
            // The duplicate is never used as a separate widget.
            w.attrs.insert(QStringLiteral("visible"),
                QStringLiteral("0"));
        }
        for (auto &c : w.children) if (c) walk(*c);
    };
    walk(mut);
    // Shrink the window when the drawer is closed so the chrome
    // sits on its own footprint with no transparent strip below.
    // Compact height covers chrome + the open-button tab that
    // pokes out at the bottom: drawer.y(=17) + button.relY(=118)
    // + button.h(~7) + a couple px of padding.
    //
    // Only the small player layouts (WACUP-style, ~185/105 px tall)
    // shrink like this.  The big Bento layout (h≈600, with the docked
    // Media Library below) keeps the drawer INTERNAL — collapsing that
    // window to the drawer-tab height clipped off the whole player.
    // So leave a large layout's window size alone.
    const QSize full = layoutNativeSize();
    const int compactH = 17 + 118 + 7 + 2;  // 144 px
    if (auto *w = window(); w && full.height() < 300) {
        w->setMinimumSize(QSize(0, 0));
        w->setMaximumSize(QSize(16777215, 16777215));
        w->resize(full.width(), open ? full.height() : compactH);
    }
    rebuildWindowRegion();
    update();
}


void HeadWindow::switchDrawerTab(int tab) {
    struct Apply {
        const char *id;
        const char *onIfThisTab;
    };
    const QString onEQ   = (tab == 1) ? QStringLiteral("1") : QStringLiteral("0");
    const QString offEQ  = (tab == 1) ? QStringLiteral("0") : QStringLiteral("1");
    const QString onOPT  = (tab == 2) ? QStringLiteral("1") : QStringLiteral("0");
    const QString offOPT = (tab == 2) ? QStringLiteral("0") : QStringLiteral("1");
    const QString onCT   = (tab == 3) ? QStringLiteral("1") : QStringLiteral("0");
    const QString offCT  = (tab == 3) ? QStringLiteral("0") : QStringLiteral("1");
    auto &mut = const_cast<qtWasabi::Layout::ResolvedWidget &>(tree());
    std::function<void(qtWasabi::Layout::ResolvedWidget &)> walk =
        [&](qtWasabi::Layout::ResolvedWidget &w) {
        // Tab on/off variants.
        if (w.id == QStringLiteral("config.tab.eq.on"))           w.attrs.insert("visible", onEQ);
        else if (w.id == QStringLiteral("config.tab.eq.off"))     w.attrs.insert("visible", offEQ);
        else if (w.id == QStringLiteral("config.tab.options.on")) w.attrs.insert("visible", onOPT);
        else if (w.id == QStringLiteral("config.tab.options.off"))w.attrs.insert("visible", offOPT);
        else if (w.id == QStringLiteral("config.tab.colorthemes.on"))   w.attrs.insert("visible", onCT);
        else if (w.id == QStringLiteral("config.tab.colorthemes.off")) w.attrs.insert("visible", offCT);
        // Content pages.
        else if (w.id == QStringLiteral("player.normal.drawer.eq"))           w.attrs.insert("visible", onEQ);
        else if (w.id == QStringLiteral("player.normal.drawer.options"))      w.attrs.insert("visible", onOPT);
        else if (w.id == QStringLiteral("player.normal.drawer.colorthemes"))  w.attrs.insert("visible", onCT);
        for (auto &c : w.children) if (c) walk(*c);
    };
    walk(mut);
}


void HeadWindow::setVisMode(int m) {
    m_visMode = qBound(0, m, 3);
    if (!settingsFile().isEmpty()) {
        QSettings s(settingsFile(), QSettings::IniFormat);
        s.setValue("visualization/mode", m_visMode);
    }
    update();
}


int HeadWindow::timeDisplayMode() const {
    return qtWasabi::privateConfigInt(
        qtWasabi::activeSkinName(),
        QStringLiteral("TimerElapsedRemaining"), 1);
}

void HeadWindow::setTimeDisplayMode(int mode) {
    qtWasabi::setPrivateConfigInt(
        qtWasabi::activeSkinName(),
        QStringLiteral("TimerElapsedRemaining"), mode == 2 ? 2 : 1);
    if (m_runtime && m_host)
        m_runtime->dispatchTitleChange(m_host->playItemDisplayTitle());
    update();
}


}  // namespace qtWasabi::head
