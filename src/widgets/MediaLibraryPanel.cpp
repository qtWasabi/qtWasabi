// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "MediaLibraryPanel.h"

#include <qtWasabi/ColorRegistry.h>
#include <qtWasabi/GammasetRegistry.h>
#include <qtWasabi/PaintCtx.h>

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <cstdlib>

namespace qtWasabi {

namespace {

QString mlIconsBaseDir() {
    if (const char *env = std::getenv("WASABIQT_ML_ICONS_DIR"))
        return QString::fromLocal8Bit(env);
    return QStringLiteral(
        "/home/snekmin/git/winamp-linux/Src/Plugins/Library");
}

// Loads a 16x16 (or larger) icon from the Winamp Library plugin
// resources and strips the Windows-classic magenta colour key
// (255, 0, 255) to alpha=0.  Cached per-process so we read each
// file exactly once.
QImage loadMlIcon(const QString &relPath) {
    if (relPath.isEmpty()) return {};
    static QHash<QString, QImage> cache;
    auto it = cache.constFind(relPath);
    if (it != cache.constEnd()) return it.value();
    const QString full = QDir(mlIconsBaseDir()).filePath(relPath);
    QImage img(full);
    if (!img.isNull()) {
        img = img.convertToFormat(QImage::Format_ARGB32);
        const QRgb magenta = qRgb(255, 0, 255) & 0x00FFFFFF;
        for (int y = 0; y < img.height(); ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                if ((row[x] & 0x00FFFFFF) == magenta) row[x] = 0u;
            }
        }
    }
    cache.insert(relPath, img);
    return img;
}

void paintChromeBevel(QPainter *p, const QRect &r,
                       const QColor &light, const QColor &dark) {
    p->setPen(light);
    p->drawLine(r.topLeft(), r.topRight());
    p->drawLine(r.topLeft(), r.bottomLeft());
    p->setPen(dark);
    p->drawLine(r.bottomLeft() + QPoint(1, 0), r.bottomRight());
    p->drawLine(r.topRight(), r.bottomRight());
}

// ─── ml_* plugin child providers ──────────────────────────────────
//
// These mirror the Winamp registration pattern where each ml_* plugin
// contributes a root tree node + a (static or dynamic) set of
// children.  Each provider returns the children the plugin would have
// populated at runtime; for plugins where we don't have the underlying
// data source (no Media Library indexer, no connected portable device,
// etc.) the provider returns an empty list and the parent shows as a
// closed folder.

// ml_local — Local Library: static set of category leaves.
QList<MlNode> children_local() {
    return {
        {"Local Media/Audio",             "Audio",
         "ml_local/resources/ti_audio_16x16x16.bmp",             false, nullptr},
        {"Local Media/Video",             "Video",
         "ml_local/resources/ti_video_16x16x16.bmp",             false, nullptr},
        {"Local Media/Most Played",       "Most Played",
         "ml_local/resources/ti_most_played_16x16x16.bmp",       false, nullptr},
        {"Local Media/Recently Added",    "Recently Added",
         "ml_local/resources/ti_recently_added_16x16x16.bmp",    false, nullptr},
        {"Local Media/Recently Modified", "Recently Modified",
         "ml_local/resources/ti_recently_modified_16x16x16.bmp", false, nullptr},
        {"Local Media/Recently Played",   "Recently Played",
         "ml_local/resources/ti_recently_played_16x16x16.bmp",   false, nullptr},
        {"Local Media/Never Played",      "Never Played",
         "ml_local/resources/ti_never_played_16x16x16.bmp",      false, nullptr},
        {"Local Media/Top Rated",         "Top Rated",
         "ml_local/resources/ti_top_rated_16x16x16.bmp",         false, nullptr},
    };
}

// ml_playlists — enumerates user-saved playlists from a local
// directory.  Winamp keeps its playlist index in the ml_pl plugin's
// data dir; we mirror the same idea with
// `$XDG_DATA_HOME/qtamp/playlists/`.
QList<MlNode> children_playlists() {
    QList<MlNode> out;
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/playlists");
    QDir d(base);
    if (!d.exists()) return out;
    const QStringList filters{
        QStringLiteral("*.m3u"), QStringLiteral("*.m3u8"),
        QStringLiteral("*.pls"), QStringLiteral("*.xspf")};
    for (const QFileInfo &fi : d.entryInfoList(filters,
                                                 QDir::Files,
                                                 QDir::Name)) {
        out.append({QStringLiteral("Playlists/") + fi.fileName(),
                    fi.completeBaseName(),
                    QStringLiteral("ml_playlists/resources/ti_playlist_16x16x16.bmp"),
                    false, nullptr});
    }
    return out;
}

// ml_bookmarks — reads simple line-per-URL bookmarks from
// `$XDG_DATA_HOME/qtamp/bookmarks.txt`.
QList<MlNode> children_bookmarks() {
    QList<MlNode> out;
    const QString path =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/bookmarks.txt");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QTextStream ts(&f);
    while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QChar('#'))) continue;
        out.append({QStringLiteral("Bookmarks/") + line,
                    line, QString(), false, nullptr});
    }
    return out;
}

// ml_history — reads recent-play log from
// `$XDG_DATA_HOME/qtamp/history.txt`, newest first.
QList<MlNode> children_history() {
    QList<MlNode> out;
    const QString path =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/history.txt");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QStringList lines;
    QTextStream ts(&f);
    while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        if (!line.isEmpty() && !line.startsWith(QChar('#')))
            lines.prepend(line);  // newest first
        if (lines.size() >= 50) break;
    }
    for (const QString &line : lines) {
        const QString label = QFileInfo(line).fileName();
        out.append({QStringLiteral("History/") + line,
                    label.isEmpty() ? line : label,
                    QString(), false, nullptr});
    }
    return out;
}

// ml_devices — enumerates portable devices via `/run/media/$USER`
// (the standard UDisks2 mount point for removable storage).  Each
// mounted volume becomes a child.
QList<MlNode> children_devices() {
    QList<MlNode> out;
    const QString user = qEnvironmentVariable("USER");
    const QString base = user.isEmpty()
        ? QString()
        : QStringLiteral("/run/media/") + user;
    if (base.isEmpty()) return out;
    QDir d(base);
    if (!d.exists()) return out;
    for (const QFileInfo &fi : d.entryInfoList(
             QDir::AllDirs | QDir::NoDotAndDotDot, QDir::Name)) {
        out.append({QStringLiteral("Devices/") + fi.fileName(),
                    fi.fileName(),
                    QStringLiteral("ml_devices/resources/generic-device-16x16.png"),
                    false, nullptr});
    }
    return out;
}

// ml_disc — enumerates optical drives by scanning /sys/block for
// `sr*` device nodes.  Each drive becomes a "DVD Drive (X:)" entry
// where X iterates D..Z to match the Windows drive-letter cosmetic
// the reference image shows.
QList<MlNode> children_disc() {
    QList<MlNode> out;
    QDir d(QStringLiteral("/sys/block"));
    QStringList drives;
    for (const QFileInfo &fi : d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (fi.fileName().startsWith(QStringLiteral("sr")))
            drives.append(fi.fileName());
    }
    std::sort(drives.begin(), drives.end());
    char letter = 'D';
    for (const QString &dev : drives) {
        out.append({QStringLiteral("Disc/") + dev,
                    QStringLiteral("DVD Drive (%1:)").arg(letter),
                    QStringLiteral("ml_disc/resources/cdrom.png"),
                    false, nullptr});
        ++letter;
        if (letter > 'Z') break;
    }
    return out;
}

// ml_online — no online-services catalogue is wired, so this returns
// no children and the root stays a closed folder.
QList<MlNode> children_online() { return {}; }

// ml_wire / podcasts — surfaces a single empty "Subscriptions" node to
// match the canonical Bento reference's layout.  With no RSS
// aggregator behind it the subscription list is static.
QList<MlNode> children_podcasts() {
    return {
        {"Podcasts/Subscriptions", "Subscriptions",
         QString(), false, nullptr},
    };
}

// Build the registry.  Same order the canonical Bento reference
// shows them: Local Library, Playlists, Online Services, Devices,
// Podcast Directory, Bookmarks, History, and finally any optical
// drives ml_disc found.
QList<MlNode> buildPluginRegistry() {
    QList<MlNode> r;
    r.append({QStringLiteral("Local Media"),    QStringLiteral("Local Library"),
              QString(),
              true,
              children_local});
    r.append({QStringLiteral("Playlists"),      QStringLiteral("Playlists"),
              QStringLiteral("ml_playlists/resources/ti_playlist_16x16x16.bmp"),
              false,
              children_playlists});
    r.append({QStringLiteral("Online Services"), QStringLiteral("Online Services"),
              QString(),
              false,
              children_online});
    r.append({QStringLiteral("Devices"),        QStringLiteral("Devices"),
              QStringLiteral("ml_devices/resources/generic-device-16x16.png"),
              false,
              children_devices});
    r.append({QStringLiteral("Podcasts"),       QStringLiteral("Podcast Directory"),
              QStringLiteral("ml_local/resources/ti_podcasts_16x16x16.bmp"),
              true,
              children_podcasts});
    r.append({QStringLiteral("Bookmarks"),      QStringLiteral("Bookmarks"),
              QStringLiteral("ml_bookmarks/resources/ti_bookmarks_16x16x16.bmp"),
              false,
              children_bookmarks});
    r.append({QStringLiteral("History"),        QStringLiteral("History"),
              QStringLiteral("ml_history/resources/ti_history_items_16x16x16.bmp"),
              false,
              children_history});
    // ml_disc registers AFTER the rest — same as gen_ml's order.
    // Show every detected optical drive as a sibling top-level node.
    QList<MlNode> discs = children_disc();
    r.append(discs);
    return r;
}

}  // namespace

MediaLibraryPanel::MediaLibraryPanel()
    : m_plugins(buildPluginRegistry()) {
    // Seed expand-state from `defaultExpanded` on the root plugins
    // (children inherit collapsed unless their own defaultExpanded
    // is true, handled in flattenVisible).
    for (const MlNode &n : m_plugins) {
        if (n.defaultExpanded) m_expanded.insert(n.invariantId);
    }
}

QList<MediaLibraryPanel::VisibleRow>
MediaLibraryPanel::flattenVisible() const {
    QList<VisibleRow> out;
    // Recursively walk the registry.  Depth 0 = top-level plugin
    // roots; depth >= 1 = their children (expanded ones contribute,
    // collapsed ones don't).
    std::function<void(const MlNode &, int, const QString &)> walk =
        [&](const MlNode &node, int depth, const QString &parentPath) {
            const bool hasProvider = static_cast<bool>(node.childProvider);
            const bool expanded =
                hasProvider && m_expanded.contains(node.invariantId);
            out.append({node, depth, hasProvider, expanded, parentPath});
            if (expanded) {
                const QList<MlNode> kids = node.childProvider();
                for (const MlNode &k : kids) {
                    walk(k, depth + 1, node.invariantId);
                }
            }
        };
    for (const MlNode &n : m_plugins) {
        walk(n, 0, QString());
    }
    return out;
}

void MediaLibraryPanel::paint(QPainter *p, PaintCtx &ctx,
                                const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) ==
        QStringLiteral("0")) return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;
    m_lastPanelRect = QRect(p->transform().map(r.topLeft()), r.size());

    p->save();

    // Skin-agnostic colour resolution.  The canonical Wasabi
    // Media Library plugin (ml.dll) registers `color.ml.*` IDs
    // at startup; any skin that wants to retheme the list view
    // simply overrides those ids.  We honour the same contract
    // here so a Bento, WinampModernPP, WACUP-stock, or future
    // user-authored skin gets its own theme applied — without
    // a single per-skin code path.  When the skin doesn't
    // declare the colour, fall back to ml.dll's stock palette
    // (flat black list, blue selection, light-grey text) so the
    // panel always renders sensibly.
    auto themed = [&](const char *id, QColor fallback) -> QColor {
        if (!ctx.colors) return fallback;
        return ctx.colors->resolve(QString::fromLatin1(id),
                                    ctx.gammasets, fallback);
    };
    const QColor bg         = themed("color.ml.list.bg",
                                       QColor(0, 0, 0));
    const QColor sep        = themed("color.ml.list.separator",
                                       QColor(34, 36, 42));
    const QColor headerBg   = themed("color.ml.list.header.bg",
                                       QColor(20, 22, 26));
    const QColor headerLine = themed("color.ml.list.header.line",
                                       QColor(48, 52, 60));
    const QColor textColor  = themed("color.ml.list.fg",
                                       QColor(220, 225, 235));
    const QColor mutedText  = themed("color.ml.list.fg.muted",
                                       QColor(160, 165, 175));
    const QColor accentBlue = themed("color.ml.list.selection.bg",
                                       QColor(58, 80, 140));
    const QColor buttonFace = themed("color.ml.button.face",
                                       QColor(40, 44, 52));
    const QColor selFg      = themed("color.ml.list.selection.fg",
                                       QColor(255, 255, 255));

    p->fillRect(r, bg);

    QFont smallFont(QStringLiteral("Tahoma"));
    smallFont.setPixelSize(11);
    const QFontMetrics smallFm(smallFont);
    const int rowH = qMax(14, smallFm.height() + 2);

    auto drawHLine = [&](int x1, int y, int x2, const QColor &c) {
        p->setPen(c);
        p->drawLine(QPoint(x1, y), QPoint(x2, y));
    };
    auto drawVLine = [&](int x, int y1, int y2, const QColor &c) {
        p->setPen(c);
        p->drawLine(QPoint(x, y1), QPoint(x, y2));
    };
    auto paintHeaderStrip = [&](const QRect &hdr,
                                  const QStringList &headers) {
        p->fillRect(hdr, headerBg);
        drawHLine(hdr.x(), hdr.bottom(), hdr.right(), headerLine);
        p->setFont(smallFont);
        p->setPen(textColor);
        int colX = hdr.x() + 6;
        const int hdrColW =
            (hdr.width() - 12) / qMax(1, int(headers.size()));
        for (int i = 0; i < headers.size(); ++i) {
            p->drawText(QRect(colX, hdr.y(), hdrColW, hdr.height()),
                        Qt::AlignVCenter | Qt::AlignLeft,
                        headers[i]);
            // Thin vertical separator between header cells —
            // matches the reference's faint column dividers.
            if (i > 0) {
                drawVLine(colX - 4, hdr.y() + 2,
                          hdr.bottom() - 2, headerLine);
            }
            colX += hdrColW;
        }
    };

    // ── Top toolbar (no bevel — flat band) ──────────────────────
    const int toolbarH = 26;
    const QRect toolbar(r.x(), r.y(), r.width(), toolbarH);
    p->fillRect(toolbar, bg);
    drawHLine(toolbar.x(), toolbar.bottom(),
              toolbar.right(), sep);

    // ml_local's "Now Filtering" toolbar — 3 small icons that
    // switch the right side between Simple (just track list),
    // Two-filter (Artist | Tracks) and Three-filter (Artist |
    // Album | Tracks) column layouts.  These are the icons the
    // canonical Bento reference renders next to Search.
    static const char *kFilterModeIcons[] = {
        "ml_local/resources/nf_simple.bmp",
        "ml_local/resources/nf_twofilters.bmp",
        "ml_local/resources/nf_threefilters.bmp",
    };
    for (int i = 0; i < 3; ++i) {
        const QImage fmIcon =
            loadMlIcon(QString::fromLatin1(kFilterModeIcons[i]));
        const QRect cell(toolbar.x() + 130 + i * 20, toolbar.y() + 5,
                          16, 16);
        if (!fmIcon.isNull()) p->drawImage(cell, fmIcon);
    }

    p->setFont(smallFont);
    p->setPen(textColor);
    p->drawText(QRect(toolbar.x() + 196, toolbar.y(),
                       54, toolbar.height()),
                Qt::AlignVCenter | Qt::AlignLeft,
                QStringLiteral("Search:"));

    const QRect searchBox(toolbar.x() + 244, toolbar.y() + 5,
                           toolbar.width() - 244 - 96,
                           toolbar.height() - 10);
    p->fillRect(searchBox, QColor(10, 12, 16));
    p->setPen(sep);
    p->drawRect(searchBox.adjusted(0, 0, -1, -1));

    const QRect clearBtn(toolbar.right() - 92, toolbar.y() + 5,
                          88, toolbar.height() - 10);
    p->fillRect(clearBtn, buttonFace);
    p->setPen(sep);
    p->drawRect(clearBtn.adjusted(0, 0, -1, -1));
    p->setPen(textColor);
    p->drawText(clearBtn, Qt::AlignCenter,
                QStringLiteral("Clear Search"));

    // ── Sidebar — flush against outer rim, no inner panel rect.
    const int sidebarW = 122;
    const int statusH  = 22;
    const QRect sidebar(r.x(),
                         toolbar.bottom() + 1,
                         sidebarW,
                         r.height() - toolbarH - statusH - 1);
    // One vertical divider between sidebar and right side — same
    // dark line that separates the toolbar from content.
    drawVLine(sidebar.right(), sidebar.y(),
              sidebar.bottom(), sep);

    const QList<VisibleRow> visible = flattenVisible();
    m_lastSidebarY     = sidebar.y() + 2;
    m_lastRowH         = rowH;
    m_lastVisibleCount = visible.size();

    int sidY = sidebar.y() + 2;
    for (int i = 0; i < visible.size(); ++i) {
        const VisibleRow &vr = visible[i];
        const QRect row(sidebar.x(), sidY,
                         sidebar.width(), rowH);
        if (i == m_sidebarSel) {
            p->fillRect(row, accentBlue);
            p->setPen(selFg);
        } else {
            p->setPen(textColor);
        }
        const int indentPx = 8 + vr.depth * 12;
        if (vr.isFolder) {
            const QImage twist = loadMlIcon(QString::fromLatin1(
                vr.isExpanded
                    ? "../General/gen_ml/resources/tree_open_16x16x16.bmp"
                    : "../General/gen_ml/resources/tree_closed_16x16x16.bmp"));
            if (!twist.isNull()) {
                const int ty = row.y() + (row.height() - 16) / 2;
                p->drawImage(QRect(row.x() + indentPx - 14,
                                    ty, 16, 16),
                             twist);
            }
        }
        int textLeft = row.x() + indentPx;
        const QImage icon = loadMlIcon(vr.node.iconRelPath);
        if (!icon.isNull()) {
            const int iconY = row.y() + (row.height() - 16) / 2;
            p->drawImage(QRect(textLeft, iconY, 16, 16), icon);
            textLeft += 18;
        }
        p->setPen(i == m_sidebarSel ? selFg : textColor);
        p->drawText(QRect(textLeft, row.y(),
                           row.right() - textLeft - 2, row.height()),
                    Qt::AlignVCenter | Qt::AlignLeft,
                    vr.node.displayLabel);
        sidY += rowH;
    }

    // Sidebar footer button "Library" — flat, no bevel.
    const QRect libBtn(sidebar.x() + 2,
                        sidebar.bottom() - 20,
                        sidebar.width() - 4, 18);
    p->fillRect(libBtn, buttonFace);
    p->setPen(sep);
    p->drawRect(libBtn.adjusted(0, 0, -1, -1));
    p->setPen(textColor);
    p->drawText(libBtn, Qt::AlignCenter, QStringLiteral("Library"));

    // ── Right side: dual-column panes + track list, all flat ────
    const QRect right(sidebar.right() + 1,
                       sidebar.y(),
                       r.right() - sidebar.right() - 1,
                       sidebar.height());
    const int paneH = (right.height() - 1) / 2;
    const int leftPaneW = right.width() / 2;
    const QRect artistPane(right.x(), right.y(), leftPaneW, paneH);
    const QRect albumPane (right.x() + leftPaneW + 1, right.y(),
                            right.width() - leftPaneW - 1, paneH);

    auto paintColumnPane = [&](const QRect &paneR,
                                const QStringList &headers,
                                const QStringList &row0) {
        p->fillRect(paneR, bg);
        const QRect hdr(paneR.x(), paneR.y(),
                         paneR.width(), rowH + 2);
        paintHeaderStrip(hdr, headers);
        const QRect r0(paneR.x(), hdr.bottom() + 1,
                        paneR.width(), rowH);
        p->fillRect(r0, accentBlue);
        p->setPen(selFg);
        int colX = r0.x() + 6;
        const int hdrColW =
            (paneR.width() - 12) / qMax(1, int(headers.size()));
        for (int i = 0; i < row0.size(); ++i) {
            p->drawText(QRect(colX, r0.y(), hdrColW, r0.height()),
                        Qt::AlignVCenter | Qt::AlignLeft,
                        row0[i]);
            colX += hdrColW;
        }
    };
    paintColumnPane(artistPane,
        {QStringLiteral("Artist"), QStringLiteral("Albums"),
         QStringLiteral("Tracks")},
        {QStringLiteral("All (0 artists)"), QStringLiteral("0"),
         QStringLiteral("0")});
    paintColumnPane(albumPane,
        {QStringLiteral("Album"), QStringLiteral("Year"),
         QStringLiteral("Tracks")},
        {QStringLiteral("All (0 albums)"), QStringLiteral(""),
         QStringLiteral("0")});
    // Vertical separator between the two top panes.
    drawVLine(albumPane.x() - 1, right.y(),
              right.y() + paneH, sep);
    // Horizontal separator between top panes and track list.
    drawHLine(right.x(), right.y() + paneH,
              right.right(), sep);

    const QRect trackPane(right.x(), right.y() + paneH + 1,
                           right.width(),
                           right.height() - paneH - 1);
    p->fillRect(trackPane, bg);
    const QStringList trackHeaders = {
        QStringLiteral("Artist"), QStringLiteral("Album"),
        QStringLiteral("Track #"), QStringLiteral("Title"),
        QStringLiteral("Length")};
    paintHeaderStrip(QRect(trackPane.x(), trackPane.y(),
                            trackPane.width(), rowH + 2),
                      trackHeaders);

    // ── Status bar — one flat dark band with a top divider line.
    const QRect status(r.x(), r.bottom() - statusH,
                        r.width(), statusH);
    p->fillRect(status, bg);
    drawHLine(status.x(), status.y(), status.right(), sep);
    const QRect playBtn(status.x() + 4, status.y() + 3,
                         60, status.height() - 6);
    p->fillRect(playBtn, buttonFace);
    p->setPen(sep);
    p->drawRect(playBtn.adjusted(0, 0, -1, -1));
    p->setPen(textColor);
    p->drawText(playBtn, Qt::AlignCenter,
                QStringLiteral("Play ▼"));
    p->setPen(mutedText);
    p->drawText(QRect(status.x() + 80, status.y(),
                       status.width() - 100, status.height()),
                Qt::AlignVCenter | Qt::AlignLeft,
                QStringLiteral("0 items in 0.001 sec."));

    p->restore();
    lastPaintedAtMs = QDateTime::currentMSecsSinceEpoch();
}

void MediaLibraryPanel::onLeftButtonDown(QPoint pos, PaintCtx &) {
    if (m_lastPanelRect.isEmpty()) return;
    const int sidebarX = m_lastPanelRect.x() + 2;
    const int sidebarW = 120;
    if (pos.x() < sidebarX || pos.x() > sidebarX + sidebarW) return;
    const int relY = pos.y() - m_lastSidebarY;
    if (relY < 0 || m_lastRowH <= 0) return;
    const int hit = relY / m_lastRowH;
    if (hit < 0 || hit >= m_lastVisibleCount) return;

    const QList<VisibleRow> visible = flattenVisible();
    if (hit >= visible.size()) return;
    const VisibleRow &vr = visible[hit];

    // Click on the twist triangle area (first ~14 px of the indent
    // gutter) toggles the folder's expand state.  Click anywhere
    // else on the row selects it.
    const int rowLeftX = sidebarX + 2;
    const int twistRight = rowLeftX + 6 + vr.depth * 12;
    if (vr.isFolder && pos.x() < twistRight) {
        if (vr.isExpanded) m_expanded.remove(vr.node.invariantId);
        else               m_expanded.insert(vr.node.invariantId);
    } else {
        m_sidebarSel = hit;
    }
    requestRepaint();
}

}  // namespace qtWasabi

