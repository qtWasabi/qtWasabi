// HeadWiring implementation — moved verbatim from the reference
// embedder's startup block (V5c).
#include <qtWasabi/head/HeadWiring.h>

#include <QDateTime>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QRect>
#include <QScreen>

#include <cstdio>
#include <cstdlib>
#include <memory>

#include <qtWasabi/PlayerHost.h>
#include <qtWasabi/SkinRuntime.h>
#include <qtWasabi/WindowHolderRegistry.h>
#include <qtWasabi/SkinView.h>
#include <qtWasabi/head/HeadWindow.h>

namespace qtWasabi::head {

void wireRuntime(HeadWindow *view, PlayerHost *host) {
    // Maki GuiObject.leftClick()/rightClick() action fallback: run the
    // delegated widget's action through this window's real dispatch.
    qtWasabi::registerSkinWidgetClickCallback(
        [view](const QString &id, bool right) -> bool {
            return view->triggerWidgetActionById(id, right);
        });
    // Maki System.showWindow / hideNamedWindow / isNamedWindowVisible
    // → the same subwindow machinery the TOGGLE action uses (the
    // vis/video drawer DETACH buttons in Winamp Modern route here).
    qtWasabi::registerNamedWindowCallback(
        [view](const QString &ref, int op) -> int {
            int result = 0;
            if (op == 1) {                       // show
                if (qtWasabi::SkinView *sv = view->ensureSubwindow(ref)) {
                    sv->show();
                    sv->raise();
                    result = 1;
                }
            } else {
                qtWasabi::SkinView *sv = view->peekSubwindow(ref);
                if (op == 0 && sv) sv->hide();   // hide
                if (sv && sv->isVisible()) {
                    result = 1;
                } else if (op == 2) {
                    // A DOCKED component (the vis/video hosted in
                    // the player's drawer holder) counts as visible
                    // — real Wasabi semantics; the drawer scripts'
                    // detach flow gates on isNamedWindowVisible
                    // before showWindow.
                    const qint64 last =
                        qtWasabi::holderLastPaintedMs(ref);
                    if (last > 0 &&
                        QDateTime::currentMSecsSinceEpoch() - last
                            < 400)
                        result = 1;
                }
            }
            if (qEnvironmentVariableIntValue("WASABIQT_TRACE_MAKI") == 1)
                fprintf(stderr, "[namedwindow] op=%d ref=%s -> %d\n",
                        op, ref.toLocal8Bit().constData(), result);
            return result;
        });
    // Maki System.setEqBand/getEqBand → host EQ store (drives the EQ
    // sliders + audio), so a skin's EQ reset / +/- buttons work.
    qtWasabi::registerSkinEqCallbacks(
        [h = host, view](int band, int val) {
            h->setEqBandValue(band, val);
            if (view) view->update();
        },
        [h = host](int band) -> int { return h->eqBandValue(band); });
    // Maki Slider.setPosition/getPosition → host slider axis (drives
    // scripted balance/volume buttons), keyed by the slider's action=.
    qtWasabi::registerSkinSliderCallbacks(
        [h = host, view](const QString &action, const QString &param,
                         int v255) {
            h->setSliderPosition(
                action, qBound(0.0, double(v255) / 255.0, 1.0), param);
            if (view) view->update();
        },
        [h = host](const QString &action,
                   const QString &param) -> int {
            const double p = h->sliderPosition(action, param);
            return p < 0.0 ? -1 : qRound(p * 255.0);
        });
    // Maki System.setVolume/getVolume (0..255) → host volume (0..100).
    qtWasabi::registerSkinVolumeCallbacks(
        [h = host, view](int v255) {
            h->setVolume(qRound(double(v255) / 255.0 * 100.0));
            if (qEnvironmentVariableIntValue("WASABIQT_TRACE_MAKI") == 1)
                fprintf(stderr, "[volume] System.setVolume(%d) -> %d%%\n",
                        v255, h->volume());
            if (view) view->update();
        },
        [h = host]() -> int {
            return qRound(double(h->volume()) / 100.0 * 255.0);
        });
    // Route Maki getStatus() through to the live QMediaPlayer
    // state so playback-state-driven scripts (setposbarvisibility.
    // maki, classicplaystatus.maki, drawer.m) see the real
    // host status instead of a hardcoded default.
    qtWasabi::registerSkinPlaybackStatusCallback(
        [h = host]() -> int {
            if (h->isPlaying()) return 1;
            if (h->isPaused())  return -1;
            return 0;
        });
    // Maki Layout.setTarget* → gotoTarget chain resizes the
    // window.  Used by drawer.m's openDrawer/closeDrawer for
    // the upper video/vis drawer — without this the drawer
    // becomes visible inside the original-sized window and
    // pushes the chrome off-screen.
    // Skin scripts fire startup resize callbacks during their
    // onScriptLoaded handlers — Bento's maximize.m calls
    // setWndToScreen() on first launch, which resizes the
    // window to fill the user's display.  That makes the
    // chrome (fixed-position widgets) look tiny inside a
    // huge window and doesn't match what the reference
    // screenshots show (Bento's native 800x600 size).
    // Block resize callbacks for the first ~750 ms after
    // skin load — covers the entire onScriptLoaded /
    // dispatchInitialResize pass.  User-initiated resize
    // events (maximize button click, etc.) fire later and
    // pass through normally.
    auto resizeUnblockAt =
        std::make_shared<qint64>(
            QDateTime::currentMSecsSinceEpoch() + 750);
    qtWasabi::registerSkinResizeCallback(
        [view, resizeUnblockAt](int w, int h) {
            if (QDateTime::currentMSecsSinceEpoch() <
                *resizeUnblockAt) {
                return;  // startup resize — ignore
            }
            // Maximize: the skin's resize() (e.g. simplemaximize.maki)
            // targets ~the screen work-area.  On Wayland a client
            // can't position itself, so a plain resize grows the
            // window but leaves it off-origin (bottom-right runs off
            // screen).  Hand the maximize to the COMPOSITOR, which
            // fills AND positions it.  General: any skin whose
            // maximize script resizes to the viewport hits this.
            if (QQuickWindow *win = view->window()) {
                const QRect avail =
                    QGuiApplication::primaryScreen()
                        ? QGuiApplication::primaryScreen()
                              ->availableGeometry()
                        : QRect();
                if (avail.isValid() && w >= avail.width() - 8 &&
                    h >= avail.height() - 8) {
                    if (win->visibility() != QWindow::Maximized)
                        win->showMaximized();
                    return;
                }
                // Restore: leave maximized state before applying the
                // smaller size, else the compositor keeps the
                // maximized geometry and ignores the resize.
                if (win->visibility() == QWindow::Maximized)
                    win->showNormal();
            }
            // WASABIQT_NO_ANIM=1 forces the snap path (handy
            // for offscreen test pipelines that grab a single
            // frame and don't want to wait out a tween).
            if (::getenv("WASABIQT_NO_ANIM"))
                view->resizeLayoutTo(QSize(w, h));
            else
                // 350 ms matches the widget-level setTarget
                // tween (configtabs.m's drawer slide) so the
                // config drawer + the video/vis drawer feel
                // consistent.
                view->animatedResizeLayoutTo(QSize(w, h), 350);
        });
}

}  // namespace qtWasabi::head
