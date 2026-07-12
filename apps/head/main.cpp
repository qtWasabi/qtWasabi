// qtwasabi-head — the framework's reference head (V5f).
//
// A pure qtWasabi head: no player of its own.  Without --connect it
// renders against FakeHost (the deterministic scripted host — the
// standalone corpus/pixel renderer); with --connect it is a remote
// head over GraphQL.  Every piece is the framework's: HeadWindow,
// wireRuntime, HeadMenu/HeadPreferences, makeTransport.
//
//   qtwasabi-head --skin <dir> [--container <ref>] [--connect <url>]
//                 [--screenshot <png>] [--probe <field>]
//                 [--list-actions]
//
// The startup sequence mirrors the reference embedder step for step
// (alpha surface before QApplication, QML toplevel, static well-known
// scripts, then the Maki runtime in its load-bearing order).
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QPainter>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <cstring>
#include <functional>

#include <qtWasabi/FakeHost.h>
#include <qtWasabi/Layout.h>
#include <qtWasabi/SkinRuntime.h>
#include <qtWasabi/SkinXml.h>
#include <qtWasabi/head/HeadPreferences.h>
#include <qtWasabi/head/HeadTransport.h>
#include <qtWasabi/head/HeadWindow.h>
#include <qtWasabi/head/HeadWiring.h>
#include <qtWasabi/remote/RemoteHost.h>

namespace qtWasabi { namespace ml { void installMlHostFactory(); } }
namespace qtWasabi { void installPleditHostFactory(); }

namespace {

QString takeStringArg(int &argc, char **argv, const char *name) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], name) == 0) {
            const QString v = QString::fromLocal8Bit(argv[i + 1]);
            for (int j = i; j < argc - 2; ++j) argv[j] = argv[j + 2];
            argc -= 2;
            return v;
        }
    }
    return QString();
}

bool takeFlag(int &argc, char **argv, const char *name) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) {
            for (int j = i; j < argc - 1; ++j) argv[j] = argv[j + 1];
            argc -= 1;
            return true;
        }
    }
    return false;
}

QString headSettingsFile() {
    const QString dir =
        QStandardPaths::writableLocation(
            QStandardPaths::GenericConfigLocation) +
        QStringLiteral("/qtWasabi");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/head.ini");
}

}  // namespace

int main(int argc, char **argv) {
    // Windowholder renderers must exist before anything paints.
    qtWasabi::ml::installMlHostFactory();
    qtWasabi::installPleditHostFactory();

    QString skinArg = takeStringArg(argc, argv, "--skin");
    // Harness compat: the pixel/corpus lanes pass --modern-skin.
    if (skinArg.isEmpty())
        skinArg = takeStringArg(argc, argv, "--modern-skin");
    // --fakehost is the head's default anyway; accept and ignore it so
    // the embedder's test lanes can drive either binary unchanged.
    takeFlag(argc, argv, "--fakehost");
    QString connectUrl = takeStringArg(argc, argv, "--connect");
    const QString containerArg = takeStringArg(argc, argv, "--container");
    const QString screenshotPath = takeStringArg(argc, argv, "--screenshot");
    const QString probeField = takeStringArg(argc, argv, "--probe");
    const bool listActions = takeFlag(argc, argv, "--list-actions");

    // No --connect: the stored active backend, else FakeHost.
    if (connectUrl.isEmpty()) {
        const QString active = qtWasabi::head::HeadPreferences::
            activeBackend(headSettingsFile());
        if (!active.isEmpty()) {
            const auto entries = qtWasabi::head::HeadPreferences::
                loadBackends(headSettingsFile());
            for (const auto &e : entries)
                if (e.name == active) { connectUrl = e.url; break; }
        }
    }

    if ((!probeField.isEmpty() || !screenshotPath.isEmpty() ||
         listActions) &&
        !qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    // Alpha before QApplication, or Wayland hands back an opaque
    // surface and the rounded corners composite black.
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setAlphaBufferSize(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("qtWasabi Head"));
    QApplication::setOrganizationName(QStringLiteral("qtWasabi"));

    // ── headless probe: one field off the first remote snapshot ──────
    if (!probeField.isEmpty() && !connectUrl.isEmpty()) {
        auto *transport = qtWasabi::head::makeTransport(
            connectUrl, headSettingsFile());
        auto *host = new qtWasabi::remote::RemoteHost(QUrl(connectUrl),
                                                      transport);
        auto printAndExit = [host, probeField]() {
            QString out;
            if (probeField == QLatin1String("playing"))
                out = host->isPlaying() ? QStringLiteral("true")
                                        : QStringLiteral("false");
            else if (probeField == QLatin1String("paused"))
                out = host->isPaused() ? QStringLiteral("true")
                                       : QStringLiteral("false");
            else if (probeField == QLatin1String("playlistCount"))
                out = QString::number(host->playlistRowCount());
            else if (probeField == QLatin1String("title"))
                out = host->songTitle();
            else if (probeField == QLatin1String("volume"))
                out = QString::number(host->volume());
            printf("%s\n", out.toLocal8Bit().constData());
            QCoreApplication::exit(0);
        };
        QTimer::singleShot(600, host, printAndExit);
        QTimer::singleShot(5000, [] { QCoreApplication::exit(2); });
        return app.exec();
    }

    // ── skin resolution ───────────────────────────────────────────────
    if (skinArg.isEmpty()) {
        const QString def = QDir::homePath() +
            QStringLiteral("/.winamp/skins/Winamp Modern");
        if (QFile::exists(def + QStringLiteral("/skin.xml")))
            skinArg = def;
    }
    QString skinXml = skinArg;
    if (!skinXml.endsWith(QLatin1String("skin.xml")))
        skinXml += QStringLiteral("/skin.xml");
    if (skinArg.isEmpty() || !QFile::exists(skinXml)) {
        fprintf(stderr, "qtwasabi-head: no skin (--skin <dir>)\n");
        return 2;
    }

    qtWasabi::SkinXml::Document doc;
    QString err;
    if (!qtWasabi::SkinXml::parse(skinXml, doc, &err)) {
        fprintf(stderr, "qtwasabi-head: parse failed: %s\n",
                err.toLocal8Bit().constData());
        return 3;
    }

    // ── host: remote over GraphQL, else the scripted FakeHost ────────
    qtWasabi::PlayerHost *host = nullptr;
    if (!connectUrl.isEmpty()) {
        auto *transport = qtWasabi::head::makeTransport(
            connectUrl, headSettingsFile());
        host = new qtWasabi::remote::RemoteHost(QUrl(connectUrl),
                                                transport);
    } else {
        host = new qtWasabi::FakeHost();
    }

    auto *view = new qtWasabi::head::HeadWindow(host);
    view->setSettingsFile(headSettingsFile());

    // Transport → repaint machinery (the same connects the reference
    // embedder wires in its window subclass).
    QObject::connect(host, &qtWasabi::PlayerHost::sourceChanged, view,
                     [view] { view->fireTitleChange(); view->update(); });
    QObject::connect(host, &qtWasabi::PlayerHost::playbackStateChanged,
                     view, [view] { view->update(); });
    QObject::connect(host, &qtWasabi::PlayerHost::metaDataChanged, view,
                     [view] { view->fireTitleChange(); view->update(); });
    QObject::connect(host, &qtWasabi::PlayerHost::playlistChanged, view,
                     [view] { view->update(); });
    // 20 fps repaint while something animates; overlayTick for
    // embedder overlays.
    auto *tick = new QTimer(view);
    tick->setInterval(50);
    QObject::connect(tick, &QTimer::timeout, view, [view, host]() {
        if (view->visMode() != 0 || host->isPlaying()) view->update();
        view->overlayTick();
    });
    tick->start();
    view->setFlag(QQuickItem::ItemIsFocusScope, true);
    view->setFlag(QQuickItem::ItemAcceptsInputMethod, true);

    QString rootContainerId = QStringLiteral("main");
    if (!containerArg.isEmpty()) {
        const QString resolved =
            qtWasabi::SkinXml::resolveContainerId(doc, containerArg);
        rootContainerId = resolved.isEmpty() ? containerArg : resolved;
    }
    view->setRootContainerId(rootContainerId);
    if (!view->load(doc, rootContainerId, "normal", &err)) {
        fprintf(stderr, "qtwasabi-head: layout failed: %s\n",
                err.toLocal8Bit().constData());
        return 4;
    }
    view->setSkinDocument(doc);
    view->applyPreferredColorTheme();

    // ── skin-agnostic action dump (the 5f gate for menu/action ids) ──
    if (listActions) {
        int count = 0;
        std::function<void(const qtWasabi::Layout::ResolvedWidget &)>
            walk = [&](const qtWasabi::Layout::ResolvedWidget &w) {
            const QString action =
                w.attrs.value(QStringLiteral("action"));
            if (!action.isEmpty()) {
                printf("%s|%s|%s\n",
                       w.id.toLocal8Bit().constData(),
                       action.toLocal8Bit().constData(),
                       w.attrs.value(QStringLiteral("param"))
                           .toLocal8Bit()
                           .constData());
                ++count;
            }
            for (const auto &c : w.children) if (c) walk(*c);
        };
        walk(view->tree());
        fprintf(stderr, "qtwasabi-head: %d actionable widgets\n", count);
        delete view;
        return count > 0 ? 0 : 6;
    }

    // ── QML toplevel ─────────────────────────────────────────────────
    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/qtwasabi/head/HeadShell.qml")));
    if (engine.rootObjects().isEmpty()) return 4;
    auto *qwin =
        qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!qwin) return 4;
    view->setParentItem(qwin->contentItem());
    view->setSize(QSizeF(view->displaySize()));
    qwin->resize(view->displaySize());
    {
        QSurfaceFormat wfmt = qwin->format();
        wfmt.setAlphaBufferSize(8);
        qwin->setFormat(wfmt);
    }
    qwin->setColor(QColor(0, 0, 0, 0));

    // ── static well-known scripts, then the Maki runtime ────────────
    auto &mutableTree =
        const_cast<qtWasabi::Layout::ResolvedWidget &>(view->tree());
    if (!::getenv("WASABIQT_NO_STATIC_SCRIPTS")) {
        qtWasabi::Layout::runKnownScripts(
            mutableTree, view->layoutNativeSize().width());
        qtWasabi::Layout::wireSteppers(mutableTree);
        view->rebuildWindowRegion();
    }
    if (!::getenv("WASABIQT_NO_RUNTIME")) {
        static qtWasabi::SkinRuntime runtime;
        view->setSkinRuntime(&runtime);
        qtWasabi::head::wireRuntime(view, host);
        runtime.setBitmapRegistry(&view->registry());
        runtime.loadScripts(doc, mutableTree);
        mutableTree.cacheResolvedRects(QPoint(0, 0),
                                       view->layoutNativeSize());
        runtime.dispatchOnScriptLoaded();
        runtime.dispatchXuiParams(mutableTree);
        if (!::getenv("WASABIQT_NO_FIRE_RESIZE")) {
            const QSize ls = view->layoutNativeSize();
            runtime.dispatchInitialResize(ls.width(), ls.height());
        }
        view->update();
    }

    qwin->setTitle(QStringLiteral("qtWasabi Head"));
    qwin->resize(view->displaySize());
    view->setAutoShrinkToRegion(true);

    // OS window resize → layout re-flow (XML units via the render
    // ratio) + Maki onResize cascade, exactly the reference embedder's
    // wiring — a resized window must re-run the skin's own reflow.
    {
        auto relayout = [view]() {
            static bool busy = false;
            auto *w = view->window();
            if (busy || !w) return;
            const double rr = view->renderRatio();
            const QSize wpx(w->width(), w->height());
            if (wpx.width() <= 0 || wpx.height() <= 0 ||
                wpx == view->displaySize())
                return;
            const QSize ws(int(wpx.width() / rr + 0.5),
                           int(wpx.height() / rr + 0.5));
            if (ws == view->layoutNativeSize()) return;
            busy = true;
            view->setAutoShrinkToRegion(false);
            view->resizeLayoutTo(ws);
            auto &t = const_cast<qtWasabi::Layout::ResolvedWidget &>(
                view->tree());
            t.cacheResolvedRects(QPoint(0, 0), ws);
            if (auto *rt = view->skinRuntime())
                rt->dispatchInitialResize(ws.width(), ws.height());
            view->rebuildWindowRegion();
            view->update();
            busy = false;
        };
        QObject::connect(qwin, &QQuickWindow::widthChanged, view,
                         [relayout](int) { relayout(); });
        QObject::connect(qwin, &QQuickWindow::heightChanged, view,
                         [relayout](int) { relayout(); });
    }

    if (::getenv("WASABIQT_HOT_RELOAD"))
        view->installHotReloadWatcher(skinXml);
    if (::getenv("WASABIQT_DUMP_MENU")) {
        QTimer::singleShot(400, view, [view]() {
            view->dumpMenusForGate();
            QCoreApplication::quit();
        });
    }

    // ── screenshot (with optional synthetic clicks) ──────────────────
    if (!screenshotPath.isEmpty()) {
        int delayMs = 250;
        // WASABIQT_FORCE_RESIZE=WxH — pre-grab window resize (the
        // corpus pins author-screenshot geometry with it).
        if (const char *fr = ::getenv("WASABIQT_FORCE_RESIZE")) {
            const QStringList wh =
                QString::fromLocal8Bit(fr).split(QLatin1Char('x'));
            if (wh.size() == 2) {
                const int w = wh[0].toInt(), h = wh[1].toInt();
                QTimer::singleShot(150, view, [view, qwin, w, h]() {
                    view->setAutoShrinkToRegion(false);
                    qwin->resize(w, h);
                });
                delayMs = qMax(delayMs, 500);
            }
        }
        if (const char *c = ::getenv("WASABIQT_CLICK_AT")) {
            const QStringList pts =
                QString::fromLocal8Bit(c).split(';', Qt::SkipEmptyParts);
            int clickDelay =
                qEnvironmentVariableIntValue("WASABIQT_CLICK_DELAY");
            if (clickDelay <= 0) clickDelay = 600;
            for (const QString &pt : pts) {
                QString spec = pt.trimmed();
                Qt::MouseButton btn = Qt::LeftButton;
                if (spec.startsWith(QLatin1Char('R'), Qt::CaseInsensitive)) {
                    btn = Qt::RightButton;
                    spec.remove(0, 1);
                }
                const QStringList xy = spec.split(',');
                if (xy.size() != 2) continue;
                const QPointF pos(xy[0].toInt(), xy[1].toInt());
                QTimer::singleShot(clickDelay, view, [view, pos, btn]() {
                    view->testClick(pos, btn);
                });
                clickDelay += 200;
            }
            delayMs = qMax(delayMs, clickDelay + 250);
        }
        QTimer::singleShot(delayMs, view, [view, qwin, screenshotPath]() {
            QImage shot = qwin->grabWindow();
            // WASABIQT_SHOT_ALPHA=1 — cut the grab to the skin's
            // shaped window region (the corpus lane compares against
            // region-cut author screenshots).
            if (qEnvironmentVariableIntValue("WASABIQT_SHOT_ALPHA") == 1) {
                shot = shot.convertToFormat(QImage::Format_ARGB32);
                const QRegion reg = view->windowRegion();
                if (!reg.isEmpty()) {
                    QImage cut(shot.size(), QImage::Format_ARGB32);
                    cut.fill(Qt::transparent);
                    QPainter cp(&cut);
                    cp.setClipRegion(reg);
                    cp.drawImage(0, 0, shot);
                    cp.end();
                    shot = cut;
                }
            }
            if (shot.save(screenshotPath)) {
                fprintf(stderr, "qtwasabi-head: wrote %s (%dx%d)\n",
                        screenshotPath.toLocal8Bit().constData(),
                        shot.width(), shot.height());
                QCoreApplication::exit(0);
            } else {
                QCoreApplication::exit(5);
            }
        });
    }

    return app.exec();
}
