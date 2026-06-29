// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "WindowHolder.h"

#include "MediaLibraryPanel.h"
#include "PlaylistPro.h"

#include <qtWasabi/BitmapRegistry.h>
#include <qtWasabi/Host.h>
#include <qtWasabi/PaintCtx.h>
#include <qtWasabi/WindowHolderRegistry.h>

#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QTransform>

#include <functional>

namespace qtWasabi {

namespace {
// Canonical Wasabi `hold=` GUIDs that callers care about.  Real
// Wasabi resolves "guid:avs" to the AVS plugin's GUID; both the
// alias form and the raw GUID string appear in real-world skins, so
// we accept either.
//
// The two GUIDs we care about (case-insensitive match):
//   • AVS visualizer slot — both `guid:avs` (alias) and
//     `guid:{0000000A-000C-0010-FF7B-01014263450C}` (canonical).
//   • DirectShow video slot — `guid:{F0816D7B-FFFC-4343-80F2-E8199AA15CC3}`.
// qtWasabi can't host the real external plugins (no DirectShow, no
// AVS plugin), but the slot positions are exactly where embedders
// want to overlay MilkDrop or paint album art.

bool eqi(const QString &s, const char *needle) {
    return s.compare(QLatin1String(needle), Qt::CaseInsensitive) == 0;
}

// Normalise any GUID spelling to the bare lowercase `{…}` form.  A
// holder's component GUID is spelled inconsistently across skins —
// `guid:{…}`, a bare `{…}` with NO prefix, or the short `avs`/`vid`
// alias.  Winamp Modern's DETACHED visualizer/video windows use a bare
// `<component param="{0000000A…}">` (no `guid:`), so a prefix-only
// match silently fails to classify them as AVS/video — the window then
// paints an opaque black void instead of hosting the visualizer.
QString bareGuidLower(const QString &ref) {
    QString b = ref.trimmed();
    if (b.startsWith(QLatin1String("guid:"), Qt::CaseInsensitive))
        b = b.mid(5).trimmed();
    return b.toLower();
}

bool isAvsHold(const QString &hold) {
    const QString b = bareGuidLower(hold);
    return b == QLatin1String("avs") ||
           b == QLatin1String("{0000000a-000c-0010-ff7b-01014263450c}");
}

bool isVideoHold(const QString &hold) {
    return bareGuidLower(hold) ==
           QLatin1String("{f0816d7b-fffc-4343-80f2-e8199aa15cc3}");
}

bool isPlaylistHold(const QString &hold) {
    return bareGuidLower(hold) ==
           QLatin1String("{45f3f7c1-a6f3-4ee6-a15e-125e92fc3f8d}");
}

bool isLibraryHold(const QString &hold) {
    return bareGuidLower(hold) ==
           QLatin1String("{6b0edf80-c9a5-11d3-9f26-00c04f39ffc6}");
}

// Resolve Winamp's short component-name aliases (pl/ml/vid/vis/avs) — which a
// skin may use in hold=/param= — to the canonical GUID, so an inline
// `<component param="guid:pl">` (HeadAMP embeds its playlist this way)
// classifies and dispatches exactly like `<windowholder hold="guid:{45F3…}">`
// (Bento/Modern).  Values that are already a GUID pass straight through.
QString canonicalHold(const QString &raw) {
    QString bare = raw.trimmed();
    if (bare.startsWith(QLatin1String("guid:"), Qt::CaseInsensitive))
        bare = bare.mid(5).trimmed();
    static const QHash<QString, QString> kAlias = {
        { QStringLiteral("pl"),    QStringLiteral("guid:{45F3F7C1-A6F3-4ee6-A15E-125E92FC3F8D}") },
        { QStringLiteral("ml"),    QStringLiteral("guid:{6B0EDF80-C9A5-11D3-9F26-00C04F39FFC6}") },
        { QStringLiteral("vid"),   QStringLiteral("guid:{F0816D7B-FFFC-4343-80F2-E8199AA15CC3}") },
        { QStringLiteral("video"), QStringLiteral("guid:{F0816D7B-FFFC-4343-80F2-E8199AA15CC3}") },
        { QStringLiteral("vis"),   QStringLiteral("guid:{0000000A-000C-0010-FF7B-01014263450C}") },
        { QStringLiteral("avs"),   QStringLiteral("guid:{0000000A-000C-0010-FF7B-01014263450C}") },
    };
    const auto it = kAlias.constFind(bare.toLower());
    return it != kAlias.constEnd() ? it.value() : raw;
}

// The component GUID a holder node hosts, spelled any of the three Wasabi
// ways (hold=/param=/component=) and with the short alias resolved.
QString heldGuid(const QHash<QString, QString> &attrs) {
    QString h = attrs.value(QStringLiteral("hold"));
    if (h.isEmpty()) h = attrs.value(QStringLiteral("param"));
    if (h.isEmpty()) h = attrs.value(QStringLiteral("component"));
    return canonicalHold(h);
}

// GUID spelled any way → bare lowercase "{...}" key.
QString guidKey(const QString &ref) {
    QString bare = ref.trimmed();
    if (bare.startsWith(QLatin1String("guid:"), Qt::CaseInsensitive))
        bare = bare.mid(5).trimmed();
    return bare.toLower();
}

// Last paint timestamp per held GUID — a DOCKED component (the vis/video
// hosted in the player's drawer holder) is "visible" in Wasabi terms;
// Maki isNamedWindowVisible must report it (the drawer detach flow gates
// on it).  Stamped on every holder paint.
QHash<QString, qint64> g_holderPaintMs;
}  // namespace

qint64 holderLastPaintedMs(const QString &holdRef) {
    return g_holderPaintMs.value(guidKey(canonicalHold(holdRef)), 0);
}

WindowHolderWidget::WindowHolderWidget() = default;
WindowHolderWidget::~WindowHolderWidget() = default;

bool WindowHolderWidget::isInteractive() const {
    const QString hold = heldGuid(attrs);
    // Registered renderer takes precedence — it reports its own
    // interactivity.  Cast-away of constness is safe: we only
    // read m_renderer's reported value, which is `const`.
    if (m_renderer) return m_renderer->isInteractive();
    if (const HolderFactory *f = lookupHolderRenderer(hold))
        return f != nullptr;  // assume registered renderers ARE interactive
    return isPlaylistHold(hold) || isLibraryHold(hold);
}

void WindowHolderWidget::paint(QPainter *p, PaintCtx &ctx,
                                const QSize &canvas) {
    if (qEnvironmentVariableIntValue("WASABIQT_TRACE_HOLDER") == 1) {
        const QRect rr = resolveRect(canvas);
        fprintf(stderr, "[holder?] id=%s guid=%s visible=%s rect=%dx%d@(%d,%d)\n",
                id.toLocal8Bit().constData(),
                heldGuid(attrs).toLocal8Bit().constData(),
                attrs.value(QStringLiteral("visible")).toLocal8Bit().constData(),
                rr.width(), rr.height(), rr.x(), rr.y());
    }
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;
    if (qEnvironmentVariableIntValue("WASABIQT_TRACE_HOLDER") == 1)
        fprintf(stderr, "[holder] hold=%s canvas=%dx%d r=%dx%d@(%d,%d) "
                "attrs h=%s relath=%s\n",
                attrs.value(QStringLiteral("hold")).toLocal8Bit().constData(),
                canvas.width(), canvas.height(), r.width(), r.height(), r.x(), r.y(),
                attrs.value(QStringLiteral("h")).toLocal8Bit().constData(),
                attrs.value(QStringLiteral("relath")).toLocal8Bit().constData());

    // The held component's GUID can be spelled three ways — `hold=`,
    // `param=`, or the legacy `component=` — all aliases in Wasabi's
    // windowholder (the `<component>` tag is itself the old name for
    // `<windowholder>`).  Read whichever is present so a skin that
    // embeds e.g. AVS via `<component param="guid:avs">` (HeadAMP) is
    // classified the same as `<windowholder hold="guid:avs">`.
    const QString hold = heldGuid(attrs);
    const bool isAvs       = isAvsHold(hold);
    const bool isVideo     = isVideoHold(hold);
    const bool isPlaylist  = isPlaylistHold(hold);
    const bool isLibrary   = isLibraryHold(hold);

    // Pluggable-renderer dispatch — consult the WindowHolderRegistry
    // first.  Any GUID a plugin has registered for is delegated to
    // its renderer.  The legacy branches below are kept as fallback
    // for GUIDs nobody claimed (AVS overlay slots, the built-in
    // MediaLibraryPanel that still serves as Phase-C visual
    // baseline).  Skin XML stays unchanged; only the back-end
    // provider for a given hold= is swappable.
    // Record that this GUID's holder painted — the docked-visibility
    // signal behind Maki isNamedWindowVisible (see holderLastPaintedMs).
    g_holderPaintMs[guidKey(hold)] = QDateTime::currentMSecsSinceEpoch();

    if (!m_rendererTried && !m_renderer) {
        if (const HolderFactory *f = lookupHolderRenderer(hold)) {
            if (*f) m_renderer = (*f)(r);
        }
        m_rendererTried = true;
    }
    if (m_renderer) {
        m_renderer->paint(p, ctx, r);
        m_lastListRect = QRect(p->transform().map(r.topLeft()), r.size());
        lastPaintedAtMs = QDateTime::currentMSecsSinceEpoch();
        return;
    }

    // Engine-rendered playlist editor / media library content.  The
    // PlaylistPro helper paints playlist rows from Host data.  For
    // the canonical Wasabi Media Library GUID we delegate to the
    // MediaLibraryPanel visual substitute (sidebar tree + dual
    // column panes + status bar) — matches reference Bento's
    // empty-library state exactly without needing a real ml.dll.
    if (isPlaylist) {
        m_lastListRect = QRect(p->transform().map(r.topLeft()), r.size());
        paintPlaylistRows(p, ctx, r, m_topRow);
        QFont qf(QStringLiteral("sans-serif"));
        qf.setPixelSize(10);
        m_lastRowH = qMax(11, QFontMetrics(qf).height() + 1);
        lastPaintedAtMs = QDateTime::currentMSecsSinceEpoch();
        return;
    }
    if (isLibrary) {
        // Construct the panel on-demand at the windowholder's rect.
        // The panel is stateless apart from sidebar selection; we
        // hold it as a member so the selection persists between
        // paints.  (Could also be a per-class singleton if cross-
        // instance state-sharing is ever desired.)
        if (!m_mlPanel) {
            m_mlPanel = std::make_unique<MediaLibraryPanel>();
            m_mlPanel->tag = QStringLiteral("medialibrarypanel");
        }
        // Forward our resolved rect by stamping the panel's attrs.
        m_mlPanel->attrs.insert(QStringLiteral("x"),
                                  QString::number(r.x()));
        m_mlPanel->attrs.insert(QStringLiteral("y"),
                                  QString::number(r.y()));
        m_mlPanel->attrs.insert(QStringLiteral("w"),
                                  QString::number(r.width()));
        m_mlPanel->attrs.insert(QStringLiteral("h"),
                                  QString::number(r.height()));
        m_mlPanel->paint(p, ctx, canvas);
        m_lastListRect = QRect(p->transform().map(r.topLeft()),
                                r.size());
        lastPaintedAtMs = QDateTime::currentMSecsSinceEpoch();
        return;
    }

    // AVS holders that aren't currently the chrome's primary
    // visualizer slot (i.e. Bento's `info.component.vis` overlapping
    // `info.component.cover`) should paint transparent rather than
    // opaque black — otherwise the cover/notfoundImage beneath them
    // gets overwritten by a black square.  The MilkDrop overlay
    // detects "AVS slot is being painted" via `lastPaintedAtMs`
    // bumps, so we keep updating that even when paint produces no
    // pixels (the GL overlay still appears).
    //
    // We treat the AVS slot as "primary" iff the parent groupdef's
    // id contains `vis` AND the host has nothing else painted at
    // the same coords — too brittle.  Practical rule: AVS slot
    // never fills the chrome.  Video slot still does (it carries
    // the album-art fallback below).
    if (!isAvs) {
        p->fillRect(r, QColor(0, 0, 0));
    }

    // For the two canonical slot kinds, record canvas-space rect +
    // bump the inherited `lastPaintedAtMs` so the embedder can detect
    // visibility independently of the visible= attr.  Critical for
    // the MilkDrop overlay's drawer-state tracking: the AVSGroup
    // declares visible=0 by default, the Maki drawer-open animation
    // flips it to 1, and the embedder's per-tick `syncMilkdropOverlay`
    // notices the timestamp updates and shows the GL overlay.
    bool drewProviderFrame = false;
    if (isAvs || isVideo) {
        // Map the holder's resolved (parent-local) top-left through the
        // painter transform to canvas space — same as the playlist/
        // library branch above.  Using QPointF(0,0) here instead lost the
        // holder's own x/y offset within its group, so an AVS component
        // placed at a non-zero local position (HeadAMP's InlineAVS at
        // 77,71 inside the Player group) had its overlay snapped to the
        // group origin.
        const QTransform t = p->transform();
        const QPointF topLeft = t.map(QPointF(r.topLeft()));
        lastCanvasRect = QRect(int(topLeft.x()), int(topLeft.y()),
                                r.width(), r.height());
        lastPaintedAtMs = QDateTime::currentMSecsSinceEpoch();

        // Embedder-supplied pixels for this slot (an offscreen MilkDrop
        // frame read back to a QImage when this holder lives in a
        // detached window with no GL of its own).  In the docked window
        // the embedder leaves the AVS slot to its GL overlay and
        // provides no frame here, so this no-ops and the overlay shows
        // through as before.
        const QImage frame = holderFrameFor(guidKey(hold), r.size());
        if (!frame.isNull()) {
            if (frame.size() == r.size())
                p->drawImage(r.topLeft(), frame);
            else
                p->drawImage(r, frame);
            drewProviderFrame = true;
        }
    }

    // Video slot fallback: paint the current track's album art
    // centered in the slot when no actual video sink is attached.
    // Real Wasabi would have a DirectShow renderer feed frames into
    // this windowholder via an HWND; qtamp doesn't ship a video
    // backend, so the slot would otherwise be a black void.
    // Surfacing the cover here lets "Switch to Video" be useful for
    // audio-only playback — a UX improvement that's strictly engine-
    // level (no per-skin XML changes required).
    if (isVideo && !drewProviderFrame && ctx.host) {
        const QImage art = ctx.host->albumArt();
        if (!art.isNull()) {
            const QImage scaled = art.scaled(
                r.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const int dx = r.x() + (r.width()  - scaled.width())  / 2;
            const int dy = r.y() + (r.height() - scaled.height()) / 2;
            p->drawImage(dx, dy, scaled);
        }
    }

    // Real Wasabi's <windowholder> embeds an external HWND and
    // ignores children.  qtWasabi can't host external surfaces, so
    // when a skin nests content inside a holder (used as a stand-in
    // for the attached surface) we recurse over the black/cover
    // background.  Skins that use the canonical hold= GUIDs don't
    // need to add children — the engine paints the appropriate
    // default on its own.
    if (!children.empty()) {
        p->save();
        p->translate(r.x(), r.y());
        QSize childCanvas(qMax(0, r.width()), qMax(0, r.height()));
        for (const auto &c : children) {
            if (c) c->paint(p, ctx, childCanvas);
        }
        p->restore();
    }
}

void WindowHolderWidget::onLeftButtonDown(QPoint pos, PaintCtx &ctx) {
    // Registered renderer claims the click first.
    if (m_renderer) {
        m_renderer->onLeftButtonDown(pos, ctx);
        requestRepaint();
        return;
    }
    const QString hold = heldGuid(attrs);
    const bool isPlaylist = isPlaylistHold(hold);
    const bool isLibrary  = isLibraryHold(hold);
    if ((!isPlaylist && !isLibrary) || !ctx.host ||
        m_lastListRect.isEmpty()) return;
    const int relY = pos.y() - m_lastListRect.y() - 2;
    if (relY < 0) return;
    const int row = m_topRow + relY / m_lastRowH;

    if (isPlaylist) {
        if (row < 0 || row >= ctx.host->playlistRowCount()) return;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_lastClickRow == row && now - m_lastClickMs < 350) {
            ctx.host->playlistPlayRow(row);
            m_lastClickMs = 0;
            m_lastClickRow = -1;
        } else {
            ctx.host->playlistSetCurrentRow(row);
            m_lastClickMs = now;
            m_lastClickRow = row;
        }
        requestRepaint();
        return;
    }

    // Library: toggle expansion on directory rows (no playback yet
    // — would need a Host::libraryAddToPlaylist hook).
    Host *host = ctx.host;
    int idx = 0;
    std::function<bool(const QString &)> walk = [&](const QString &parent)
        -> bool {
            const int n = host->libraryRowCount(parent);
            for (int i = 0; i < n; ++i) {
                const QString path = host->libraryRowPath(parent, i);
                const bool isDir = host->libraryRowHasChildren(parent, i);
                const bool exp = isDir && m_libraryExpansion.value(path, false);
                if (idx++ == row) {
                    if (isDir) {
                        m_libraryExpansion[path] = !exp;
                        requestRepaint();
                    }
                    return true;
                }
                if (exp && walk(path)) return true;
            }
            return false;
        };
    walk(QString());
}

void WindowHolderWidget::onMouseMove(QPoint pos, PaintCtx &ctx) {
    if (m_renderer) { m_renderer->onMouseMove(pos, ctx); requestRepaint(); }
}

void WindowHolderWidget::onLeftButtonUp(QPoint pos, PaintCtx &ctx) {
    if (m_renderer) m_renderer->onLeftButtonUp(pos, ctx);
}

void WindowHolderWidget::onMouseWheel(QPoint pos, int steps, PaintCtx &ctx) {
    if (m_renderer) {
        m_renderer->onMouseWheel(pos, steps, ctx);
        requestRepaint();
        return;
    }
    // Built-in list fallback (no registered renderer): scroll the row
    // window.  steps>0 = wheel up = toward the top.  Generic across any
    // skin's playlist/library holder regardless of GUID spelling.
    const QString hold = heldGuid(attrs);
    if (!isPlaylistHold(hold) && !isLibraryHold(hold)) return;
    const int next = qMax(0, m_topRow - steps);
    if (next != m_topRow) { m_topRow = next; requestRepaint(); }
}

bool WindowHolderWidget::capturesMouse() const {
    // Resolve the held GUID via the canonical hold=/param=/component=
    // path (with alias) — a skin may embed the playlist/library through
    // any of the three spellings (HeadAMP uses `<component
    // param="guid:pl">`).  Reading the raw `hold=` attr alone made the
    // holder refuse to capture the mouse for those skins, so list rows
    // couldn't be selected or scrolled.
    const QString h = heldGuid(attrs);
    return isPlaylistHold(h) || isLibraryHold(h);
}

bool WindowHolderWidget::isSolidHitRegion() const {
    const QString h = heldGuid(attrs);
    return isPlaylistHold(h) || isLibraryHold(h);
}

}  // namespace qtWasabi
