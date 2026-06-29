// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <qtWasabi/SkinQuickItem.h>

#include <qtWasabi/HitCtx.h>
#include <qtWasabi/PaintCtx.h>
#include <qtWasabi/SkinXml.h>
#include <qtWasabi/SkinRuntime.h>
#include <qtWasabi/TreePainter.h>
#include <qtWasabi/Host.h>
#include <qtWasabi/Widget.h>

#include <QHash>
#include <QImage>
#include <QMetaObject>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QPointer>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QSet>
#include <QTimer>
#include <QVariantAnimation>
#include <QWindow>

namespace qtWasabi {

SkinQuickItem::SkinQuickItem(QQuickItem *parent) : QQuickItem(parent) {
    setFlag(QQuickItem::ItemHasContents, true);
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);

    // Wire the Maki repaint callback so script setXmlParam mutations
    // trigger a repaint.  QPointer + QueuedConnection match the old
    // SkinView pattern so callbacks from the VM thread land safely
    // on the GUI thread.
    QPointer<SkinQuickItem> self(this);
    registerSkinRepaintCallback([self]() {
        if (auto *v = self.data()) {
            // Recompute the window region synchronously so the next
            // paint's clip reflects the just-mutated attr.  Maki
            // script dispatch runs on the GUI thread, so a direct
            // call is safe; the previous queued-update path left
            // the region stale (with drawer.y from XML defaults
            // instead of after-script values), producing the
            // "white edge rectangles on the drawer" visual bug.
            //
            // Skip the region rebuild while a tween is in flight
            // (drawer slide, layout grow): it's a tree walk + alpha
            // → QRegion pass + setMask roundtrip per frame, which
            // dominated the animation's per-frame cost and caused
            // the visible chop.  rebuildWindowRegion runs once at
            // the animation's finish handler to settle the input
            // region back to the final shape.
            // Coalesce the (expensive) region rebuild — a single Maki
            // onTimer tick can issue many setAttr calls, and rebuilding
            // synchronously per call saturates the GUI thread, starving
            // click/hover hit-testing and the file dialog.  update() is
            // cheap (Qt coalesces repaints) so keep it synchronous.
            if (widgetAnimationsActive() == 0 && !v->m_layoutAnimActive)
                v->scheduleRegionRebuild();
            v->update();
        }
    });
}

SkinQuickItem::~SkinQuickItem() {
    registerSkinRepaintCallback({});
}

void SkinQuickItem::setActiveGammaset(const QString &name) {
    m_gammasets.setActiveGammaset(name);
    m_registry.setGammasetRegistry(&m_gammasets);
    m_fonts.invalidateGlyphCache();
    m_alphaCache.clear();
    update();
}

void SkinQuickItem::animatedResizeLayoutTo(const QSize &target, int durationMs) {
    if (!target.isValid() || target.width() <= 0 || target.height() <= 0)
        return;
    const QSize from = m_nativeSize;
    if (from == target || durationMs <= 0) {
        resizeLayoutTo(target);
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
                // End of tween: clear the suppression flag, settle the
                // QQuickWindow at exact target dimensions, and rebuild
                // the input region (suppressed during the tween).
                m_layoutAnimActive = false;
                if (auto *w = window()) w->resize(m_nativeSize);
                rebuildWindowRegion();
                fireTargetReached();
            });
    } else {
        m_resizeAnim->stop();
        m_layoutAnimActive = false;
    }
    // Pre-grow the QQuickWindow once to the larger of from/target so
    // the chrome's painted extent has room throughout the animation.
    // Resizing a Wayland surface every animation tick is expensive
    // (each frame triggers a fresh xdg_surface ack + buffer attach
    // handshake) and visibly lags behind the animation, leaving the
    // chrome clipped short of the target.  resizeLayoutTo will see
    // m_layoutAnimActive=true and skip the window resize for the
    // duration of the tween.
    if (auto *w = window()) {
        const int wantH = qMax(from.height(), target.height());
        const int wantW = qMax(from.width(),  target.width());
        if (w->width() != wantW || w->height() != wantH)
            w->resize(wantW, wantH);
    }
    m_layoutAnimActive = true;
    m_resizeAnim->setStartValue(from);
    m_resizeAnim->setEndValue(target);
    m_resizeAnim->setDuration(durationMs);
    beginAnimatedResize();
    m_resizeAnim->start();
}

void SkinQuickItem::resizeLayoutTo(const QSize &size) {
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0) return;
    if (qEnvironmentVariableIntValue("WASABIQT_TRACE_RESIZE") == 1) {
        fprintf(stderr, "[SkinQuickItem] resizeLayoutTo %dx%d (was %dx%d)\n",
                size.width(), size.height(),
                m_nativeSize.width(), m_nativeSize.height());
    }
    m_nativeSize = size;
    m_tree.attrs.insert(QStringLiteral("w"),
                        QString::number(size.width()));
    m_tree.attrs.insert(QStringLiteral("h"),
                        QString::number(size.height()));
    setSize(QSizeF(size));
    // Also resize the hosting QQuickWindow so the OS surface matches the
    // new layout.  Suppressed during an active layout tween — see
    // animatedResizeLayoutTo: it pre-grows the window once at start and
    // settles it back at finish, so per-frame resizes only churn the
    // Wayland surface without changing what the user perceives.
    if (!m_layoutAnimActive) {
        if (auto *w = window()) {
            w->resize(size);
        }
    }
    m_alphaCache.clear();
    // rebuildWindowRegion is a tree walk + alpha→region pass + QQuickWindow
    // setMask roundtrip — significant per-frame cost.  Skip it during a
    // layout tween (the input region is in flux anyway while the drawer
    // slides) and let the finish handler rebuild it once at settle.
    if (!m_layoutAnimActive) rebuildWindowRegion();
    emit layoutNativeSizeChanged();
    update();
}

bool SkinQuickItem::load(const SkinXml::Document &doc,
                          const QString &containerId,
                          const QString &layoutId,
                          QString *errMsg) {
    if (!Layout::expandLayout(doc, containerId, layoutId, m_tree, errMsg))
        return false;
    m_doc = &doc;
    m_registry.loadFromDocument(doc);
    m_fonts.loadFromDocument(doc);
    m_gammasets.loadFromDocument(doc);
    m_colors.loadFromDocument(doc);
    m_registry.setGammasetRegistry(&m_gammasets);

    // The menubar (File/Play/Options/…) is POSITIONED by the Maki VM running
    // the skin's own menualign.maki — `offset += tmp.getAutoWidth()` per
    // item — during the embedder's loadScripts/dispatchOnScriptLoaded pass.
    // Each menu group's WIDTH (its hover/click area) is the autoWidth of its
    // label bitmap, resolved here now the registry is populated — the engine
    // resolves group autoWidth, the script only sets x.  Without this the
    // groups keep relatw=1 and every menu item's hover spans the full bar.
    Layout::resolveBitmapAutoWidths(m_tree, m_registry);
    // Pair every sysregion="-N" cutout layer with its sibling chrome
    // layer.  These are TALL narrowing-strip cutouts that get baked
    // INTO the chrome bitmap's alpha (BitmapRegistry::chromeImageFor
    // uses DestinationOut).  Without this the drawer-narrowing strips
    // along the player.main left/right edges paint as solid white
    // stripes instead of being cut away.  Only the per-bitmap bake
    // remains — the final-buffer pass (Layout::paintRegionCutouts)
    // that handled small corner masks stays disabled because it
    // damaged chrome at any other layer overlapping those corners.
    m_registry.setChromeCutouts(
        Layout::collectChromeCutouts(m_tree, m_registry));

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
    setSize(QSizeF(m_nativeSize));
    m_alphaCache.clear();
    rebuildWindowRegion();

    emit layoutNativeSizeChanged();
    update();
    return true;
}

// ── Scene Graph paint ───────────────────────────────────────────────
//
// Bridge approach: paint the resolved widget tree to an offscreen
// QImage using the existing TreePainter, then upload the result as a
// single QSGTexture and present it via QSGSimpleTextureNode.
// Functionally identical to SkinView::paintEvent, just hosted in a
// QQuickItem container so:
//   - we benefit from the QQuickWindow's transparent + frameless flags
//   - alpha-aware hit-test via contains() override
//   - Wayland setMask via QWindow::setMask
//   - QPropertyAnimation can drive widget attrs
// The QImage-painter can be incrementally replaced with per-widget
// QSGNodes (layer → QSGSimpleTextureNode, text → QSGGeometryNode per
// glyph, grid → 3-slice, vis → custom geometry, etc.).
void SkinQuickItem::paintInto(QPainter *p, const QSize &canvas) {
    if (m_host) {
        TreePainter::paintTree(p, m_tree, m_registry, m_fonts,
                                canvas, m_host);
    } else {
        TreePainter::paintTree(p, m_tree, m_registry, m_fonts,
                                canvas, m_resolver);
    }
}

void SkinQuickItem::scheduleRegionRebuild() {
    if (m_regionRebuildPending) return;     // coalesce the burst
    m_regionRebuildPending = true;
    // Queued so it runs after the current Maki dispatch returns to the
    // event loop — many setAttr in one onTimer tick collapse to one
    // rebuild, using the post-script attribute values.
    QMetaObject::invokeMethod(this, [this]() {
        m_regionRebuildPending = false;
        if (widgetAnimationsActive() == 0 && !m_layoutAnimActive)
            rebuildWindowRegion();
    }, Qt::QueuedConnection);
}

void SkinQuickItem::rebuildWindowRegion() {
    m_windowRegion = Layout::computeWindowRegion(
        m_tree, m_registry, m_nativeSize);
    // Pushing setMask to a QQuickWindow on Wayfire/wlroots before
    // its first frame has been committed prevents the xdg-toplevel
    // from ever registering with the compositor.  Even after the
    // window is mapped, setMask can race with the surface state and
    // cause the window to disappear from foreign-toplevel
    // enumeration.  Skin the mask off for QQuickWindow path entirely
    // — the input region was a nice-to-have (click-through on
    // transparent chrome), not a requirement.  The visual is
    // unaffected because the QSGTexture upload already carries the
    // chrome's alpha.  WASABIQT_INPUT_REGION=1 re-enables it.
    // setMask path: defer the FIRST mask via a 100ms one-shot timer
    // (the first commit needs to land before Wayfire accepts a
    // setMask without dropping the surface), then update on every
    // subsequent region rebuild.  isExposed() never goes true for a
    // QQuickWindow on Wayfire/Asahi so we can't gate on that.
    // setMask defines the OS-level INPUT region on Wayland (wlroots/
    // Wayfire): pointer events outside the mask pass straight through to
    // the desktop.  Our computeWindowRegion silhouette does NOT cover the
    // full chrome (e.g. the enlarged playlist column + the ML tab strip),
    // so applying it made those areas unclickable — the user's "I click
    // straight through the window".  The window already has an alpha
    // buffer, so transparent chrome pixels are transparent WITHOUT a mask;
    // the mask was only a click-through-on-corners nicety.  Default OFF
    // (full-window input region); opt back in with WASABIQT_INPUT_REGION=1.
    if (::getenv("WASABIQT_INPUT_REGION")) {
        if (auto *w = window()) {
            if (!m_maskInitialised) {
                m_maskInitialised = true;
                QPointer<SkinQuickItem> self(this);
                QPointer<QQuickWindow> wp(w);
                QTimer::singleShot(150, this, [self, wp]() {
                    if (auto *s = self.data(); s && wp) {
                        if (s->m_windowRegion.isEmpty()) wp->setMask(QRegion());
                        else                            wp->setMask(s->m_windowRegion);
                        if (::getenv("WASABIQT_TRACE_MASK"))
                            fprintf(stderr,
                              "[mask] FIRST setMask rectCount=%d alpha=%d\n",
                              int(s->m_windowRegion.rectCount()),
                              wp->format().alphaBufferSize());
                    }
                });
            } else {
                if (m_windowRegion.isEmpty()) w->setMask(QRegion());
                else                          w->setMask(m_windowRegion);
            }
        }
    }
    update();
}

// Bottom-most row with a non-zero alpha pixel — auto-shrink target.
static int paintedBottomEdge(const QImage &alpha) {
    if (alpha.isNull()) return -1;
    for (int y = alpha.height() - 1; y >= 0; --y) {
        for (int x = 0; x < alpha.width(); ++x) {
            if (qAlpha(alpha.pixel(x, y)) > 16) return y + 1;
        }
    }
    return -1;
}

QSGNode *SkinQuickItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
    if (m_nativeSize.isEmpty()) return nullptr;
    auto *win = window();
    if (!win) return old;

    const QSize sz = m_nativeSize;
    // Re-instantiate any GroupXFade widget whose `groupid` attr has
    // changed since the last paint — that's how the configtarget /
    // options-page scripts switch between option pages, by mutating
    // `skin.config.target.groupid` from "optionsgroup.drawers" to
    // "optionsgroup.menus" (or whichever the user clicked).
    if (m_doc) Layout::resolveGroupXFadePages(m_tree, *m_doc);
    QImage buf(sz, QImage::Format_ARGB32_Premultiplied);
    buf.fill(Qt::transparent);
    {
        QPainter bp(&buf);
        paintInto(&bp, sz);
        // The old pixel-subtraction corner-rounding pipeline (chrome
        // bitmap pre-bake in BitmapRegistry::chromeImageFor + final-
        // buffer DestinationOut via Layout::paintRegionCutouts) is
        // now disabled by default.  The window region (setMask on
        // the QQuickWindow) handles outer corner rounding, and Maki
        // scripts drive the per-widget visibility / clipping —
        // running the legacy subtraction on top damaged chrome
        // bitmaps that happened to overlap a sysregion="-N" cutout
        // (most visibly the player.main bottom-right area near the
        // CONFIG / winamp-flash buttons after a drawer-close).
        // Set WASABIQT_LEGACY_CUTOUTS=1 to opt back in.
        if (::getenv("WASABIQT_LEGACY_CUTOUTS"))
            Layout::paintRegionCutouts(bp, m_tree, m_registry, sz);
    }

    // Auto-fit: match the QQuickWindow's height to the painted
    // chrome's bottom edge so transparent areas below the chrome
    // don't bleed through to the desktop AND so a re-grown chrome
    // (drawer reopen after a closed state's shrink) doesn't get
    // clipped by a too-small window.  8-px hysteresis avoids
    // sub-pixel jitter for widgets that paint at the edge.  Clamps
    // to m_nativeSize so we never exceed the layout's authored
    // height.
    //
    // CRITICAL: only run after Wayfire has actually mapped the
    // surface (`isExposed() == true`).  Resizing the QQuickWindow
    // from inside the first updatePaintNode (before the surface has
    // committed its first buffer) aborts the wayland commit and the
    // window stays invisible on screen.  isExposed() goes true the
    // moment the compositor signals the surface as visible.
    if (m_autoShrink && !buf.isNull() && win && win->isExposed()) {
        const int curH = win->height();
        const int curW = win->width();
        if (widgetAnimationsActive() > 0 || m_layoutAnimActive) {
            // Pre-grow the window to the full layout height for the
            // duration of any widget tween (drawer slide).  Resizing
            // a Wayland surface per frame is expensive — the queued
            // reconfigures lag behind the animation, leaving the
            // chrome clipped short of the final target.  Auto-shrink
            // resumes once the animation finishes.
            const int wantH = m_nativeSize.height();
            if (wantH > curH) {
                QMetaObject::invokeMethod(win, [win, curW, wantH]() {
                    win->resize(curW, wantH);
                }, Qt::QueuedConnection);
            }
        } else {
            const int bottom = paintedBottomEdge(buf);
            const int target = qMin(bottom, m_nativeSize.height());
            if (target > 0 && qAbs(curH - target) > 8) {
                if (::getenv("WASABIQT_TRACE_MAKI"))
                    ::fprintf(stderr,
                        "[autofit] %dx%d -> %dx%d (painted bottom %d)\n",
                        curW, curH, curW, target, bottom);
                QMetaObject::invokeMethod(win, [win, curW, target]() {
                    win->resize(curW, target);
                }, Qt::QueuedConnection);
            }
        }
    }

    // Build (or recycle) the texture node.
    auto *node = static_cast<QSGSimpleTextureNode *>(old);
    if (!node) {
        node = new QSGSimpleTextureNode();
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Nearest);   // pixel-perfect bitmap chrome
    }
    QSGTexture *tex = win->createTextureFromImage(
        buf, QQuickWindow::TextureHasAlphaChannel);
    node->setTexture(tex);
    node->setRect(QRectF(0, 0, sz.width(), sz.height()));

    // Stash the painted buffer's alpha channel for hit-testing.  We
    // store the full buffer (cheap — a few hundred kB) keyed on the
    // root widget pointer so contains() can sample it directly.
    m_alphaCache.clear();
    m_alphaCache.insert(&m_tree, std::move(buf));

    return node;
}

// ── Hit-test ────────────────────────────────────────────────────────

bool SkinQuickItem::contains(const QPointF &point) const {
    // Bounds check first.
    if (!QRectF(QPointF(0, 0), QSizeF(m_nativeSize)).contains(point))
        return false;
    // If we haven't painted yet, fall through to the inclusive default
    // so the first click reaches us.  We treat the painted-buffer alpha
    // as the single source of truth.
    auto it = m_alphaCache.constFind(&m_tree);
    if (it == m_alphaCache.constEnd() || it->isNull()) return true;
    const QImage &buf = it.value();
    const int x = qBound(0, int(point.x()), buf.width() - 1);
    const int y = qBound(0, int(point.y()), buf.height() - 1);
    const QRgb px = buf.pixel(x, y);
    return qAlpha(px) > 16;
}

namespace {
ImageSizeFn wrapImageSize(Layout::ImageSizeResolver imageSize, void *ud) {
    if (!imageSize) return {};
    return [imageSize, ud](const QString &id) { return imageSize(id, ud); };
}
}  // namespace

const Layout::ResolvedWidget *
SkinQuickItem::topmostWidgetAt(QPoint pointInLayout, bool actionOnly) const {
    HitCtx ctx;
    ctx.actionOnly = actionOnly;
    ctx.requireIdOrInteractive = true;
    // Resolve bitmap natural size for widgets without explicit w/h
    // (buttons commonly omit both — Wasabi's `<button image="..."/>`
    // implicitly sizes to the bitmap).  Without this resolver
    // Widget::hitTest's self-bbox falls through to width<=0 and
    // returns null, so hover/hit-test fails on every bare-bitmap
    // button.  Same resolver the alphaHitTestList path uses.
    auto &reg = const_cast<BitmapRegistry &>(m_registry);
    ctx.imageSize = [&reg](const QString &img) {
        QImage src = reg.imageFor(img);
        // NStatesButton convention: when the bare image id isn't a
        // registered bitmap, fall back to the `0`-suffixed variant
        // (`repeat` → `repeat0` etc).  Without this, NStates buttons
        // with no explicit w/h on the widget look invisible to
        // hit-test (their image isn't found, the bbox falls through
        // to width<=0, and the widget rejects every click).
        if (src.isNull() && !img.isEmpty()) {
            src = reg.imageFor(img + QStringLiteral("0"));
        }
        return src.isNull() ? QSize() : src.size();
    };
    auto it = m_alphaCache.constFind(&m_tree);
    if (it != m_alphaCache.constEnd() && !it->isNull())
        ctx.alphaBuf = &(*it);
    QRect bbox;
    return const_cast<Widget &>(m_tree).hitTest(
        pointInLayout, QPoint(0, 0), m_nativeSize, ctx, &bbox);
}

QList<const Layout::ResolvedWidget *>
SkinQuickItem::alphaHitTestList(QPoint pointInLayout, bool actionOnly,
                                 Layout::ImageSizeResolver imageSize,
                                 void *imageSizeUserdata) const {
    HitCtx ctx;
    ctx.actionOnly = actionOnly;
    ctx.requireIdOrInteractive = true;
    ctx.imageSize = wrapImageSize(imageSize, imageSizeUserdata);
    auto it = m_alphaCache.constFind(&m_tree);
    if (it != m_alphaCache.constEnd() && !it->isNull())
        ctx.alphaBuf = &(*it);
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

void SkinQuickItem::testClick(QPointF localPoint) {
    QMouseEvent press(QEvent::MouseButtonPress, localPoint, localPoint, localPoint,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    mousePressEvent(&press);
    QMouseEvent rel(QEvent::MouseButtonRelease, localPoint, localPoint, localPoint,
                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    mouseReleaseEvent(&rel);
}

QString SkinQuickItem::dispatchClickAt(QPointF localPoint) {
    const QPoint p = localPoint.toPoint();
    const auto hits = alphaHitTestList(p, /*actionOnly=*/false);
    for (const auto *w : hits) {
        if (!w || w->id.isEmpty()) continue;
        const int fired = fireWidgetEvent(w->id, L"onLeftClick");
        if (fired > 0) return w->id;
        // Event bubbling: a click is delivered to the embed_xui surface, but
        // the onLeftClick handler is often bound on an ENCLOSING group — e.g.
        // a tab strip binds switch.X.onLeftClick while the real hit target is
        // the nested mousetrap button (id reused across all tabs).  If the
        // leaf didn't consume the click, walk up its parent chain and fire
        // onLeftClick on each id'd ancestor until one handles it.  Receiver-
        // gated, so passive ancestors cost nothing.  This is what lets the
        // Maki tab flow (switch_X.onLeftClick -> switchToX) run.
        for (const Widget *a = w->parentWidget; a; a = a->parentWidget) {
            if (a->id.isEmpty()) continue;
            if (fireWidgetEvent(a->id, L"onLeftClick") > 0) return a->id;
        }
    }
    // onAction caller — Wasabi Buttons dispatch their `action=` attr
    // through the script-overridable onAction(action, param, x, y, …) when
    // a plain click wasn't consumed.  Scripts hook onAction to drive tab
    // switching, drawer toggles, and layout reflow — the behaviour the
    // wireTabs/force-visible crutches currently fake.  Receiver-gated
    // (fireWidgetActionEvent only fires where onAction is actually bound),
    // so passive widgets pay nothing, and it only runs on a real click so
    // static renders stay byte-identical.  Wasabi action syntax is "VERB"
    // or "VERB;param"; the onAction handler may sit on the button itself
    // or on an enclosing group, so walk up the parent chain.
    for (const auto *w : hits) {
        if (!w || w->id.isEmpty()) continue;
        const QString act = w->attrs.value(QStringLiteral("action"));
        if (act.isEmpty()) continue;
        const int semi      = act.indexOf(QLatin1Char(';'));
        const QString verb  = semi < 0 ? act : act.left(semi);
        const QString param = semi < 0 ? QString() : act.mid(semi + 1);
        for (const Widget *cur = w; cur; cur = cur->parentWidget) {
            if (cur->id.isEmpty()) continue;
            const int fired = fireWidgetActionEvent(
                cur->id, verb, param, p.x(), p.y(), 0, 0, w->id);
            if (fired > 0) return w->id;
        }
    }
    return QString();
}

// ── Mouse handling ──────────────────────────────────────────────────

// Resize-grab edge detection shared by press + hover.  Edges get an 8px
// margin; corners a larger 20px zone so the bottom-right resize spot is
// easy to hit (the classic Winamp grip).  w/h are the item's logical size,
// which equals the toplevel's, so the margins sit at the real window edge.
static Qt::Edges resizeEdgesAt(const QPointF &lp, qreal w, qreal h) {
    const qreal EM = 8.0;    // edge margin
    const qreal CM = 20.0;   // corner margin (larger)
    const bool L  = lp.x() <= EM,      R  = lp.x() >= w - EM;
    const bool T  = lp.y() <= EM,      B  = lp.y() >= h - EM;
    const bool Lc = lp.x() <= CM,      Rc = lp.x() >= w - CM;
    const bool Tc = lp.y() <= CM,      Bc = lp.y() >= h - CM;
    if (Rc && Bc) return Qt::RightEdge | Qt::BottomEdge;
    if (Lc && Bc) return Qt::LeftEdge  | Qt::BottomEdge;
    if (Rc && Tc) return Qt::RightEdge | Qt::TopEdge;
    if (Lc && Tc) return Qt::LeftEdge  | Qt::TopEdge;
    Qt::Edges e;
    if (L)      e |= Qt::LeftEdge;
    else if (R) e |= Qt::RightEdge;
    if (T)      e |= Qt::TopEdge;
    else if (B) e |= Qt::BottomEdge;
    return e;
}

bool SkinQuickItem::beginEdgeResize(const QPointF &localPos,
                                    const QPoint &globalPos) {
    auto *w = window();
    if (!w) return false;
    const Qt::Edges edges = resizeEdgesAt(localPos, width(), height());
    if (!edges) return false;
    // Native compositor resize (X11, and Wayland compositors that honour
    // it).  Returns false on Wayfire → fall back to the manual drag.
    if (w->startSystemResize(edges)) return true;
    m_resizeEdges        = edges;
    m_resizeStartGeom    = w->geometry();
    m_resizeOriginGlobal = globalPos;
    return true;
}

// Build a PaintCtx wired to this item's live registries + host so a
// captured widget (slider/scrollbar) can read/write its position exactly
// as the paint path does.  A captured slider needs a real host in its
// PaintCtx so SliderWidget::onMouseMove can write the new value back.
qtWasabi::PaintCtx SkinQuickItem::makeEventCtx() {
    qtWasabi::PaintCtx ctx{};
    ctx.bmp       = &m_registry;
    ctx.font      = &m_fonts;
    ctx.host      = m_host;
    ctx.gammasets = &m_gammasets;
    ctx.colors    = &m_colors;
    ctx.resolver  = m_resolver;
    return ctx;
}

void SkinQuickItem::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) { QQuickItem::mousePressEvent(e); return; }
    const QPointF lp = e->position();
    // Press-routing: a capture-style widget (slider, scrollbar thumb)
    // takes the press for press→move→release dragging.  Route the press to
    // its onLeftButtonDown and hold the capture; never start a window
    // drag/resize underneath it.  Passive widgets fall through to the
    // onLeftClick dispatch + drag path below.
    if (qtWasabi::Widget *w =
            const_cast<qtWasabi::Widget *>(topmostWidgetAt(lp.toPoint(), false))) {
        if (w->capturesMouse()) {
            m_activeWidget = w;
            qtWasabi::PaintCtx ctx = makeEventCtx();
            w->onLeftButtonDown(lp.toPoint(), ctx);
            update();
            e->accept();
            return;
        }
    }
    const QString consumedId = dispatchClickAt(lp);
    if (!consumedId.isEmpty()) {
        update();
        e->accept();
        return;
    }
    // Empty-area press near a window edge/corner → resize the toplevel.
    // Frameless windows have no native border to grab, so we expose a grab
    // margin (see resizeEdgesAt).  Checked only AFTER widget hit testing so
    // it never steals a button/list click near the border.
    if (beginEdgeResize(lp, e->globalPosition().toPoint())) {
        e->accept();
        return;
    }
    // Empty-area click → start window drag.  Wayland prefers
    // QWindow::startSystemMove; on other platforms fall back to manual
    // tracking via mouseMove.
    if (auto *w = window()) {
        if (w->startSystemMove()) {
            e->accept();
            return;
        }
        m_dragging = true;
        m_dragOriginGlobal = e->globalPosition().toPoint();
        m_dragWindowStart  = w->position();
        e->accept();
        return;
    }
    QQuickItem::mousePressEvent(e);
}

void SkinQuickItem::mouseMoveEvent(QMouseEvent *e) {
    // While a capture widget holds the press, feed it move events so
    // the slider thumb tracks the cursor.  Takes priority over drag/resize.
    if (m_activeWidget) {
        qtWasabi::PaintCtx ctx = makeEventCtx();
        m_activeWidget->onMouseMove(e->position().toPoint(), ctx);
        update();
        e->accept();
        return;
    }
    if (m_resizeEdges) {
        if (auto *w = window()) {
            const QPoint d = e->globalPosition().toPoint() - m_resizeOriginGlobal;
            const QRect  g = m_resizeStartGeom;
            const int MINW = 300, MINH = 160;
            int nx = g.x(), ny = g.y(), nw = g.width(), nh = g.height();
            if (m_resizeEdges & Qt::RightEdge)
                nw = qMax(MINW, g.width()  + d.x());
            if (m_resizeEdges & Qt::BottomEdge)
                nh = qMax(MINH, g.height() + d.y());
            if (m_resizeEdges & Qt::LeftEdge) {
                nw = qMax(MINW, g.width() - d.x());
                nx = g.right() - nw + 1;
            }
            if (m_resizeEdges & Qt::TopEdge) {
                nh = qMax(MINH, g.height() - d.y());
                ny = g.bottom() - nh + 1;
            }
            // Size change with an unchanged origin (right/bottom) is honoured
            // by Wayland; left/top also nudge the origin (may be clamped by
            // the compositor).  The resize triggers the main-loop relayout.
            w->setGeometry(nx, ny, nw, nh);
        }
        e->accept();
        return;
    }
    if (m_dragging) {
        if (auto *w = window()) {
            const QPoint d = e->globalPosition().toPoint() - m_dragOriginGlobal;
            w->setPosition(m_dragWindowStart + d);
        }
        e->accept();
        return;
    }
    QQuickItem::mouseMoveEvent(e);
}

void SkinQuickItem::mouseReleaseEvent(QMouseEvent *e) {
    // End any capture-widget drag (slider) with onLeftButtonUp.
    if (m_activeWidget) {
        qtWasabi::PaintCtx ctx = makeEventCtx();
        m_activeWidget->onLeftButtonUp(e->position().toPoint(), ctx);
        m_activeWidget = nullptr;
        update();
    }
    m_dragging = false;
    m_resizeEdges = {};
    QQuickItem::mouseReleaseEvent(e);
}

void SkinQuickItem::wheelEvent(QWheelEvent *e) {
    // Route a vertical scroll into the widget under the cursor so list
    // holders (playlist / library) and any scroll-aware widget react —
    // generic, no per-skin wiring.  One notch (120 eighths-of-a-degree)
    // = one step; positive steps = wheel-up = scroll toward the top.
    const QPointF lp = e->position();
    qtWasabi::Widget *w = const_cast<qtWasabi::Widget *>(
        topmostWidgetAt(lp.toPoint(), false));
    // Only handle the wheel over a list-style holder (playlist / library);
    // elsewhere leave it for other behaviour.
    if (w && w->capturesMouse()) {
        // Accumulate sub-notch deltas (Wayland sends e.g. 95, not 120) and
        // emit one scroll step per full notch, keeping the remainder so a
        // slow fine-grained wheel still scrolls.
        m_wheelAccumY += e->angleDelta().y();
        const int steps = m_wheelAccumY / 120;
        if (steps != 0) {
            m_wheelAccumY -= steps * 120;
            qtWasabi::PaintCtx ctx = makeEventCtx();
            w->onMouseWheel(lp.toPoint(), steps, ctx);
            update();
        }
        e->accept();
        return;
    }
    m_wheelAccumY = 0;   // reset when the cursor leaves a list
    QQuickItem::wheelEvent(e);
}

// ── Hover routing ─────────────────────────────────────────────────
// Wasabi buttons paint their `hoverImage=` variant while the cursor
// is over them.  Qt hover events arrive at the QQuickItem level
// (setAcceptHoverEvents was already enabled in the ctor); we just
// have to route them into the same widget virtual chain mouse
// press/release already uses.  topmostWidgetAt re-runs the same
// hit-test path as mousePressEvent so the widget chosen for hover
// is exactly the one the user would also click.

void SkinQuickItem::hoverMoveEvent(QHoverEvent *e) {
    const QPoint p = e->position().toPoint();
    // Resize-grab affordance: a directional resize cursor inside the edge
    // grab margin of the (frameless) toplevel, matching mousePressEvent.
    {
        const Qt::Edges he = resizeEdgesAt(e->position(), width(), height());
        const bool L = he & Qt::LeftEdge,  R = he & Qt::RightEdge;
        const bool T = he & Qt::TopEdge,   B = he & Qt::BottomEdge;
        Qt::CursorShape cs = Qt::ArrowCursor;
        if ((L && T) || (R && B))      cs = Qt::SizeFDiagCursor;
        else if ((R && T) || (L && B)) cs = Qt::SizeBDiagCursor;
        else if (L || R)               cs = Qt::SizeHorCursor;
        else if (T || B)               cs = Qt::SizeVerCursor;
        const Qt::CursorShape cur = cursor().shape();
        const bool wasResize =
            cur == Qt::SizeFDiagCursor || cur == Qt::SizeBDiagCursor ||
            cur == Qt::SizeHorCursor   || cur == Qt::SizeVerCursor;
        if (cs != Qt::ArrowCursor) {
            if (cs != cur) setCursor(cs);
            QQuickItem::hoverMoveEvent(e);
            return;
        }
        if (wasResize) unsetCursor();
    }
    qtWasabi::Widget *now =
        const_cast<qtWasabi::Widget *>(topmostWidgetAt(p, false));
    if (::getenv("WASABIQT_TRACE_HOVER"))
        fprintf(stderr, "[hover] (%d,%d) -> tag=%s id=%s\n",
            p.x(), p.y(),
            now ? now->tag.toLocal8Bit().constData() : "-",
            now ? now->id.toLocal8Bit().constData() : "(null)");
    if (now == m_hoverWidget) {
        // Same widget — keep onMouseMove firing for sliders /
        // scrollbars / any widget that wants per-pixel feedback,
        // but skip the leave/enter pair (cheaper).
        if (now) {
            PaintCtx ctx{};
            now->onMouseMove(p, ctx);
        }
        QQuickItem::hoverMoveEvent(e);
        return;
    }
    PaintCtx ctx{};
    if (m_hoverWidget) m_hoverWidget->onMouseLeave(ctx);
    m_hoverWidget = now;
    if (now) now->onMouseMove(p, ctx);
    QQuickItem::hoverMoveEvent(e);
}

void SkinQuickItem::hoverLeaveEvent(QHoverEvent *e) {
    if (m_hoverWidget) {
        PaintCtx ctx{};
        m_hoverWidget->onMouseLeave(ctx);
        m_hoverWidget = nullptr;
    }
    QQuickItem::hoverLeaveEvent(e);
}

}  // namespace qtWasabi
