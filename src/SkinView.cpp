// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <qtWasabi/HitCtx.h>
#include <qtWasabi/Host.h>
#include <qtWasabi/PaintCtx.h>
#include <qtWasabi/SkinView.h>
#include <qtWasabi/SkinXml.h>
#include <qtWasabi/SkinRuntime.h>
#include <qtWasabi/TreePainter.h>
#include <qtWasabi/Widget.h>

#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPointer>
#include <QVariantAnimation>
#include <QWindow>

namespace qtWasabi {

SkinView::SkinView(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    // The repaint callback is registered in load() under this view's own
    // script-root scope (not here) — registering it from the ctor would
    // bind it to whichever root is active at construction (the player's),
    // clobbering the player's Maki repaint.
}

SkinView::~SkinView() {
    // Tear the subwindow's runtime + repaint callback down under THIS
    // view's root so neither touches the player's context, then drop the
    // runtime (its own destructor scopes itself + forgets the root).
    if (m_runtime) {
        ScopedScriptRoot guard(&m_tree);
        registerSkinRepaintCallback({});
        delete m_runtime;
        m_runtime = nullptr;
    }
}

void SkinView::setActiveGammaset(const QString &name) {
    m_gammasets.setActiveGammaset(name);
    m_registry.setGammasetRegistry(&m_gammasets);   // clears tint cache
    m_fonts.invalidateGlyphCache();                 // glyphs need re-tinting too
    update();
}

void SkinView::rebuildWindowRegion() {
    m_windowRegion = Layout::computeWindowRegion(
        m_tree, m_registry, m_nativeSize);
    // On Wayland (Qt 6.x), QWidget::setMask is forwarded to
    // wl_surface.set_input_region, so transparent areas of the chrome
    // become click-through to the desktop / windows behind us.  On
    // X11/Windows it's the conventional window-shape mask.  We keep
    // the paint-side clip in paintEvent for compositors that ignore
    // the input region — that ensures the visual is correct either
    // way, and setMask just adds the input-region behaviour where the
    // compositor honours it.  Empty region = rectangular widget.
    if (m_windowRegion.isEmpty())
        clearMask();
    else
        setMask(m_windowRegion);
    update();
}

// Compute the bottom-most row with a non-zero alpha pixel.  Used by
// auto-shrink to crop the OS window to the actual painted extent.
namespace {
int paintedBottomEdge(const QImage &alpha) {
    if (alpha.isNull()) return -1;
    for (int y = alpha.height() - 1; y >= 0; --y) {
        for (int x = 0; x < alpha.width(); ++x) {
            if (qAlpha(alpha.pixel(x, y)) > 16) return y + 1;
        }
    }
    return -1;
}
}  // namespace

void SkinView::setPaintedAlpha(QImage img) {
    m_paintedAlpha = std::move(img);
    if (!m_autoShrink || m_paintedAlpha.isNull()) return;

    // Find the actual painted bottom edge and shrink the QWidget if
    // it's significantly shorter than the current widget height.
    // Never grow — Maki-driven setTargetH owns the layout-extension
    // case via resizeLayoutTo.  An 8-px hysteresis avoids sub-pixel
    // jitter when widgets paint at-or-near the layout edge.
    const int bottom = paintedBottomEdge(m_paintedAlpha);
    if (bottom > 0 && bottom + 8 <= height()) {
        if (::getenv("WASABIQT_TRACE_MAKI"))
            ::fprintf(stderr, "[autoshrink] %dx%d -> %dx%d (painted bottom)\n",
                      width(), height(), width(), bottom);
        resize(width(), bottom);
    }
}

void SkinView::resizeLayoutTo(const QSize &size) {
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0)
        return;
    m_nativeSize = size;
    // Sync the layout root's w/h attrs so relatw/relath children
    // resolve against the new size on the next paint.
    m_tree.attrs.insert(QStringLiteral("w"),
                        QString::number(size.width()));
    m_tree.attrs.insert(QStringLiteral("h"),
                        QString::number(size.height()));
    resize(size);
    rebuildWindowRegion();
}

void SkinView::animatedResizeLayoutTo(const QSize &target, int durationMs) {
    if (!target.isValid() || target.width() <= 0 || target.height() <= 0)
        return;
    const QSize from = m_nativeSize;
    if (from == target || durationMs <= 0) {
        resizeLayoutTo(target);
        // Async VM entries fire under THIS subwindow's root (the same
        // ownership rule as Maki timers) — never whichever window
        // happens to be active.
        ScopedScriptRoot guard(&m_tree);
        fireTargetReached();
        return;
    }
    if (!m_resizeAnim) {
        m_resizeAnim = new QVariantAnimation(this);
        m_resizeAnim->setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(m_resizeAnim, &QVariantAnimation::valueChanged,
            this, [this](const QVariant &v) {
                resizeLayoutTo(v.toSize());
            });
        QObject::connect(m_resizeAnim, &QVariantAnimation::finished,
            this, [this]() {
                ScopedScriptRoot guard(&m_tree);
                fireTargetReached();
            });
    } else {
        m_resizeAnim->stop();
    }
    m_resizeAnim->setStartValue(from);
    m_resizeAnim->setEndValue(target);
    m_resizeAnim->setDuration(durationMs);
    beginAnimatedResize();
    m_resizeAnim->start();
}

bool SkinView::load(const SkinXml::Document &doc,
                    const QString &containerId,
                    const QString &layoutId,
                    QString *errMsg) {
    if (!Layout::expandLayout(doc, containerId, layoutId, m_tree, errMsg))
        return false;
    m_registry.loadFromDocument(doc);
    m_fonts.loadFromDocument(doc);
    m_gammasets.loadFromDocument(doc);
    m_colors.loadFromDocument(doc);
    m_registry.setGammasetRegistry(&m_gammasets);

    // Native size: prefer explicit w/h, fall back to minimum_w/h.
    auto attrInt = [&](const QString &k, int def = 0) {
        auto it = m_tree.attrs.constFind(k);
        if (it == m_tree.attrs.constEnd()) return def;
        bool ok = false;
        int v = it.value().toInt(&ok);
        return ok ? v : def;
    };
    int w = attrInt(QStringLiteral("w"));
    int h = attrInt(QStringLiteral("h"));
    if (w <= 0) w = attrInt(QStringLiteral("minimum_w"), 354);
    if (h <= 0) h = attrInt(QStringLiteral("minimum_h"), 280);
    m_nativeSize = QSize(w, h);
    resize(m_nativeSize);

    // Run the static well-known-script pass the main window also runs, so a
    // container subwindow (Playlist Editor, Media Library, …) gets its
    // titlebar centred + streaks sized (the title-resize known-script), its
    // steppers wired, and every widget's pixel rect resolved.  Without this a
    // subwindow's titlebar text overlaps the chrome and relat-sized widgets
    // collapse to (0,0) — the main window dodged it only because its own load
    // path runs these.
    Layout::runKnownScripts(m_tree, m_nativeSize.width());
    Layout::wireSteppers(m_tree);
    // Resolve bitmap-label autoWidths (menubar item widths) now the
    // registry is populated — the VM's menualign.maki sets only each menu
    // group's x, so its width (the hover/click area) comes from here.
    Layout::resolveBitmapAutoWidths(m_tree, m_registry);
    m_tree.cacheResolvedRects(QPoint(0, 0), m_nativeSize);

    // Run THIS subwindow's Maki scripts through its OWN per-window runtime,
    // scoped to this view's tree root so registration coexists with the
    // player's (real Wasabi: one engine, per-window object resolution).
    // The menubar (File/Playlist/Sort/Help) is laid out by the skin's own
    // menualign.maki running here — no static wireMenuAlign fallback.
    if (!m_runtime) m_runtime = new SkinRuntime();
    {
        ScopedScriptRoot guard(&m_tree);
        QPointer<SkinView> self(this);
        registerSkinRepaintCallback([self]() {
            if (auto *v = self.data())
                QMetaObject::invokeMethod(v, [v]() { v->rebuildWindowRegion(); },
                                          Qt::QueuedConnection);
        });
        m_runtime->setBitmapRegistry(&m_registry);
        m_runtime->loadScripts(doc, m_tree);
        // menualign.m's setXmlParam("x", …) moved the menu items — re-resolve.
        m_tree.cacheResolvedRects(QPoint(0, 0), m_nativeSize);
        m_runtime->dispatchOnScriptLoaded();
        m_runtime->dispatchXuiParams(m_tree);
    }

    // Apply the skin's window region — pixels not covered by any
    // sysregion= layer get masked off so the player keeps its
    // chrome shape (rounded corners, drawer cutouts, etc.) instead
    // of leaking opaque bitmap pixels into the desktop.  An empty
    // region means the skin defines no sysregion mask, so we leave
    // the widget rectangular and rely on chrome bitmap alpha.
    rebuildWindowRegion();
    return true;
}

void SkinView::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    const QSize s = e->size();
    if (!s.isValid() || s.isEmpty()) return;
    // A script-driven resize (resizeLayoutTo) already synced m_nativeSize
    // and the root attrs BEFORE calling resize(); skip the duplicate work.
    // An EXTERNAL resize (manual edge-drag, compositor) arrives here with
    // a stale m_nativeSize — without this sync the layout keeps resolving
    // at the old size and the content over/under-flows the window.
    if (s != m_nativeSize) {
        m_nativeSize = s;
        m_tree.attrs.insert(QStringLiteral("w"), QString::number(s.width()));
        m_tree.attrs.insert(QStringLiteral("h"), QString::number(s.height()));
    }
    m_tree.cacheResolvedRects(QPoint(0, 0), s);
    // Fire the Maki per-object onResize cascade (scoped to this subwindow's
    // root) so the skin's own scripts reflow — then re-resolve, since the
    // cascade mutates geometry attrs.
    if (m_runtime) {
        ScopedScriptRoot guard(&m_tree);
        m_runtime->dispatchInitialResize(s.width(), s.height());
        m_tree.cacheResolvedRects(QPoint(0, 0), s);
    }
    rebuildWindowRegion();
    update();
}

void SkinView::paintEvent(QPaintEvent *) {
    // Render skin into a raster buffer with the window-region clip
    // applied — QPainter's clip works reliably on QImage targets,
    // unlike the Wayland-backed widget surface where setClipRegion
    // is silently ignored on some compositors.  Then blit the
    // pre-clipped image onto the surface in CompositionMode_Source
    // so its alpha (zero outside the region) overwrites the
    // surface alpha unconditionally.
    QImage buf(size(), QImage::Format_ARGB32_Premultiplied);
    buf.fill(Qt::transparent);
    {
        QPainter bp(&buf);
        if (!m_windowRegion.isEmpty() &&
            !qEnvironmentVariableIsSet("WASABIQT_NO_REGION_CLIP"))
            bp.setClipRegion(m_windowRegion);
        if (m_host) {
            // Pass the colour + gammaset registries so a container subwindow
            // (Playlist Editor, Media Library, …) themes exactly like the main
            // window: the titlebar chrome tints, and the list rows resolve
            // their wasabi.list.* colours (Winamp Modern blue) instead of the
            // classic-Winamp fallback the no-registry overload produces.
            QRect ctBbox;
            int   ctTopOut = 0;
            // A container subwindow renders its chrome in the active or
            // inactive state per the OS window focus (the Wasabi
            // activeAlpha/inactiveAlpha convention) — unfocused windows
            // (every container in a multi-window screenshot) show the dim
            // inactive titlebar/buttons, matching real Winamp.
            TreePainter::paintTree(&bp, m_tree, m_registry, m_fonts,
                                   size(), m_host, &m_gammasets, &m_colors,
                                   -1, 0, &ctBbox, &ctTopOut, 1,
                                   isActiveWindow());
        } else {
            TreePainter::paintTree(&bp, m_tree, m_registry, m_fonts,
                                    size(), m_resolver);
        }
    }

    QPainter p(this);
    p.setClipping(false);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(0, 0, buf);

    // Stash the painted alpha for alphaHitTest().  Cheap (a few
    // hundred kB for a 354x510 layout), refreshed each paint so the
    // hit-test always reads the current frame's opacity.
    m_paintedAlpha = std::move(buf);
}

namespace {
// Wrap the embedder's ImageSizeResolver (which takes a userdata
// pointer) into the simpler `ImageSizeFn` callable that Widget::
// hitTest's HitCtx expects.  Returns an empty function when the
// resolver itself is null.
ImageSizeFn wrapImageSize(Layout::ImageSizeResolver imageSize, void *ud) {
    if (!imageSize) return {};
    return [imageSize, ud](const QString &id) { return imageSize(id, ud); };
}
}  // namespace

const Layout::ResolvedWidget *
SkinView::alphaHitTest(QPoint pointInLayout, bool actionOnly,
                        Layout::ImageSizeResolver imageSize,
                        void *imageSizeUserdata) const {
    HitCtx ctx;
    ctx.actionOnly = actionOnly;
    ctx.requireIdOrInteractive = true;
    ctx.imageSize = wrapImageSize(imageSize, imageSizeUserdata);
    if (!m_paintedAlpha.isNull()) ctx.alphaBuf = &m_paintedAlpha;
    QRect bbox;
    return const_cast<Widget &>(m_tree).hitTest(
        pointInLayout, QPoint(0, 0), m_nativeSize, ctx, &bbox);
}

QList<const Layout::ResolvedWidget *>
SkinView::alphaHitTestList(QPoint pointInLayout, bool actionOnly,
                            Layout::ImageSizeResolver imageSize,
                            void *imageSizeUserdata) const {
    HitCtx ctx;
    ctx.actionOnly = actionOnly;
    ctx.requireIdOrInteractive = true;
    ctx.imageSize = wrapImageSize(imageSize, imageSizeUserdata);
    if (!m_paintedAlpha.isNull()) ctx.alphaBuf = &m_paintedAlpha;
    QList<Widget *> hits;
    ctx.collect = &hits;
    QRect bbox;
    const_cast<Widget &>(m_tree).hitTest(
        pointInLayout, QPoint(0, 0), m_nativeSize, ctx, &bbox);
    QList<const Layout::ResolvedWidget *> out;
    out.reserve(hits.size());
    for (auto *h : hits) out.append(h);
    return out;
}

// ── Mouse handling ──────────────────────────────────────────────────
// Container subwindows (Playlist Editor, Media Library, detached
// Video/Visualizer) are frameless toplevels — the OS gives them no
// titlebar to grab, so input routing has to do it itself, exactly as
// the main player window (SkinQuickItem) does.

namespace {
// C-callable image-size resolver for the hit-test: bare-bitmap buttons
// (`<button image="…"/>` with no explicit w/h) size to their bitmap.
// Falls back to the `0`-suffixed variant for NStates buttons, mirroring
// the main window's resolver.
QSize skinViewImageSize(const QString &img, void *ud) {
    auto *reg = static_cast<BitmapRegistry *>(ud);
    if (!reg || img.isEmpty()) return QSize();
    QImage src = reg->imageFor(img);
    if (src.isNull()) src = reg->imageFor(img + QStringLiteral("0"));
    return src.isNull() ? QSize() : src.size();
}
}  // namespace

PaintCtx SkinView::makeEventCtx() {
    PaintCtx ctx{};
    ctx.bmp       = &m_registry;
    ctx.font      = &m_fonts;
    ctx.host      = m_host;
    ctx.gammasets = &m_gammasets;
    ctx.colors    = &m_colors;
    ctx.resolver  = m_resolver;
    return ctx;
}

QString SkinView::dispatchClickAt(QPoint p) {
    // Resolve onLeftClick / onAction against THIS subwindow's script root,
    // not the player's — fireWidgetEvent looks up the active root's widget
    // registry, so a subwindow click must run under its own root.
    ScopedScriptRoot guard(&m_tree);
    const auto hits = alphaHitTestList(p, /*actionOnly=*/false,
                                       skinViewImageSize, &m_registry);
    // onLeftClick — fire on the topmost opaque widget, then bubble up
    // its parent chain (a tab strip binds the handler on the enclosing
    // group while the hit target is the nested mousetrap button).
    for (const auto *w : hits) {
        if (!w || w->id.isEmpty()) continue;
        if (fireWidgetEvent(w->id, L"onLeftClick") > 0) return w->id;
        for (const Widget *a = w->parentWidget; a; a = a->parentWidget) {
            if (a->id.isEmpty()) continue;
            if (fireWidgetEvent(a->id, L"onLeftClick") > 0) return a->id;
        }
    }
    // onAction — a Wasabi Button dispatches its `action=` verb when a
    // plain click wasn't consumed.  Window-management verbs act on THIS
    // subwindow (a subwindow's CLOSE must close itself, not the host the
    // engine's dispatchAction would route it to); the script-overridable
    // onAction handles the rest, falling back to the host transport verbs.
    for (const auto *w : hits) {
        if (!w || w->id.isEmpty()) continue;
        const QString act = w->attrs.value(QStringLiteral("action"));
        if (act.isEmpty()) continue;
        const int     semi  = act.indexOf(QLatin1Char(';'));
        const QString verb  = semi < 0 ? act : act.left(semi);
        const QString param = semi < 0 ? QString() : act.mid(semi + 1);
        const QString V     = verb.toUpper();
        // Window-local chrome buttons close/minimise the subwindow itself.
        if (V == QLatin1String("CLOSE"))    { close();          return w->id; }
        if (V == QLatin1String("MINIMIZE")) { showMinimized();  return w->id; }
        // Scripted onAction (tab switch, drawer toggle, …) — bubble up.
        bool fired = false;
        for (const Widget *cur = w; cur; cur = cur->parentWidget) {
            if (cur->id.isEmpty()) continue;
            if (fireWidgetActionEvent(cur->id, verb, param,
                                      p.x(), p.y(), 0, 0, w->id) > 0) {
                fired = true;
                break;
            }
        }
        if (fired) return w->id;
        // Global transport / system verbs (PLAY, NEXT, PREV, SYSMENU, …)
        // route to the host.  Parented to this view so any dialog/menu
        // anchors correctly.
        if (m_host && dispatchAction(verb, m_host, this)) return w->id;
        // The widget IS a control (it declares an action) — consume the
        // click so an unimplemented verb never falls through to a window
        // drag.  Only bare chrome (no action, no handler) drags.
        return w->id;
    }
    return QString();
}

// Frameless subwindows have no native border; expose a grab margin for
// resize (8px edges, 20px corners) — mirrors SkinQuickItem's main window.
static Qt::Edges skinViewResizeEdges(const QPoint &lp, int w, int h) {
    const int EM = 8, CM = 20;
    const bool L = lp.x() <= EM,     R = lp.x() >= w - EM;
    const bool T = lp.y() <= EM,     B = lp.y() >= h - EM;
    const bool Lc = lp.x() <= CM,    Rc = lp.x() >= w - CM;
    const bool Tc = lp.y() <= CM,    Bc = lp.y() >= h - CM;
    if (Rc && Bc) return Qt::RightEdge | Qt::BottomEdge;
    if (Lc && Bc) return Qt::LeftEdge  | Qt::BottomEdge;
    if (Rc && Tc) return Qt::RightEdge | Qt::TopEdge;
    if (Lc && Tc) return Qt::LeftEdge  | Qt::TopEdge;
    Qt::Edges ed;
    if (L)      ed |= Qt::LeftEdge;
    else if (R) ed |= Qt::RightEdge;
    if (T)      ed |= Qt::TopEdge;
    else if (B) ed |= Qt::BottomEdge;
    return ed;
}

void SkinView::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) { QWidget::mousePressEvent(e); return; }
    const QPoint p = e->position().toPoint();
    const Qt::Edges edges = skinViewResizeEdges(p, width(), height());

    // A capture-style widget (slider thumb, scrollbar, list holder) takes
    // the press for press→move→release dragging — but NOT in the resize
    // margin, so the window edge stays grabbable.
    if (const Layout::ResolvedWidget *hit =
            alphaHitTest(p, /*actionOnly=*/false,
                         skinViewImageSize, &m_registry)) {
        if (hit->capturesMouse() && !edges) {
            m_activeWidget = const_cast<Widget *>(hit);
            PaintCtx ctx = makeEventCtx();
            m_activeWidget->onLeftButtonDown(p, ctx);
            update();
            e->accept();
            return;
        }
    }

    // Passive widget: fire its onLeftClick / action (buttons near a corner,
    // e.g. close/minimise, win here before the resize grab).
    if (!dispatchClickAt(p).isEmpty()) {
        update();
        e->accept();
        return;
    }

    // Edge/corner → resize the toplevel.  Wayfire declines
    // startSystemResize, so fall back to manual geometry tracking.
    if (edges) {
        if (QWindow *wh = window() ? window()->windowHandle() : nullptr) {
            if (!wh->startSystemResize(edges)) {
                m_resizeEdges        = edges;
                m_resizeStartGeom    = wh->geometry();
                m_resizeOriginGlobal = e->globalPosition().toPoint();
            }
            e->accept();
            return;
        }
    }

    // Empty area (the titlebar / bare chrome) → drag the frameless toplevel.
    if (QWindow *wh = window() ? window()->windowHandle() : nullptr) {
        if (wh->startSystemMove()) { e->accept(); return; }
        m_dragging         = true;
        m_dragOriginGlobal = e->globalPosition().toPoint();
        m_dragWindowStart  = wh->position();
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void SkinView::mouseMoveEvent(QMouseEvent *e) {
    // While a capture widget holds the press, feed it move events so the
    // slider thumb tracks the cursor.  Takes priority over the drag.
    if (m_activeWidget) {
        PaintCtx ctx = makeEventCtx();
        m_activeWidget->onMouseMove(e->position().toPoint(), ctx);
        update();
        e->accept();
        return;
    }
    if (m_resizeEdges) {
        if (QWindow *wh = window() ? window()->windowHandle() : nullptr) {
            const QPoint d = e->globalPosition().toPoint() - m_resizeOriginGlobal;
            const QRect  g = m_resizeStartGeom;
            const int MINW = 275, MINH = 116;
            int nx = g.x(), ny = g.y(), nw = g.width(), nh = g.height();
            if (m_resizeEdges & Qt::RightEdge)  nw = qMax(MINW, g.width()  + d.x());
            if (m_resizeEdges & Qt::BottomEdge) nh = qMax(MINH, g.height() + d.y());
            if (m_resizeEdges & Qt::LeftEdge) {
                nw = qMax(MINW, g.width() - d.x());
                nx = g.right() - nw + 1;
            }
            if (m_resizeEdges & Qt::TopEdge) {
                nh = qMax(MINH, g.height() - d.y());
                ny = g.bottom() - nh + 1;
            }
            wh->setGeometry(nx, ny, nw, nh);
        }
        e->accept();
        return;
    }
    if (m_dragging) {
        if (QWindow *wh = window() ? window()->windowHandle() : nullptr) {
            const QPoint d = e->globalPosition().toPoint() - m_dragOriginGlobal;
            wh->setPosition(m_dragWindowStart + d);
        }
        e->accept();
        return;
    }
    QWidget::mouseMoveEvent(e);
}

void SkinView::wheelEvent(QWheelEvent *e) {
    const int steps = e->angleDelta().y() / 120;   // one notch = 120
    const QPoint p = e->position().toPoint();
    if (steps != 0) {
        if (const Layout::ResolvedWidget *hit =
                alphaHitTest(p, /*actionOnly=*/false,
                             skinViewImageSize, &m_registry)) {
            PaintCtx ctx = makeEventCtx();
            const_cast<Widget *>(hit)->onMouseWheel(p, steps, ctx);
            update();
            e->accept();
            return;
        }
    }
    QWidget::wheelEvent(e);
}

void SkinView::mouseReleaseEvent(QMouseEvent *e) {
    if (m_activeWidget) {
        PaintCtx ctx = makeEventCtx();
        m_activeWidget->onLeftButtonUp(e->position().toPoint(), ctx);
        m_activeWidget = nullptr;
        update();
    }
    m_dragging = false;
    m_resizeEdges = {};
    QWidget::mouseReleaseEvent(e);
}

}  // namespace qtWasabi
