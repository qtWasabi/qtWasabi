// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "MlHostWidget.h"
#include "MlLibraryWindow.h"

#include "../widgets/MultiColumnList.h"
#include "../widgets/SectionFrame.h"
#include "../widgets/TreeList.h"

#include <qtWasabi/BitmapRegistry.h>
#include <qtWasabi/ColorRegistry.h>
#include <qtWasabi/GammasetRegistry.h>
#include <qtWasabi/Host.h>
#include <qtWasabi/PaintCtx.h>

#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QMenu>
#include <QCursor>
#include <QSet>

#include <cstdlib>
#include <cstdio>
#include <memory>

namespace qtWasabi {
namespace ml {

namespace {

QColor themed(PaintCtx &ctx, const char *id, QColor fallback) {
    if (!ctx.colors) return fallback;
    return ctx.colors->resolve(QString::fromLatin1(id),
                                ctx.gammasets, fallback);
}

// ── wa_dlg / genex bridge ─────────────────────────────────────────
// These live in wasabi-compat (same libqtwasabi); declared extern "C"
// here to avoid the win32/ include path.  HBITMAP/HWND are void* in the
// compat layer, so the loose void* signatures are ABI-compatible.
extern "C" {
void *qtwasabi_make_genex(const unsigned *colors24);
void  qtwasabi_set_genskin_bitmap(void *genex);
void  WADlg_init(void *hwnd);
int   WADlg_getColor(int idx);
// Renders the real wa_dlg owner-draw silver button (9-sliced from the
// genex) into a w*h ARGB32 buffer.
void  qtwasabi_wadlg_button_argb(int w, int h, int pressed, unsigned *out);
}

// Draw the real wa_dlg silver button face at `r` (label text, if any, is
// drawn by the caller — wa_dlg's GetDlgItemText is stubbed empty here).
inline void drawWadlgButton(QPainter *p, const QRect &r, bool pressed) {
    if (r.width() <= 0 || r.height() <= 0) return;
    QImage img(r.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    qtwasabi_wadlg_button_argb(r.width(), r.height(), pressed ? 1 : 0,
                               reinterpret_cast<unsigned *>(img.bits()));
    p->drawImage(r.topLeft(), img);
}

// Resolve the 24 WADLG_* colours from the live skin, synthesise + install
// the genex, and run WADlg_init so WADlg_getColor returns the skin's
// palette (verify via WASABIQT_TRACE_WADLG).  This primes the colours so
// the wa_dlg draw calls used by the ML chrome resolve to the active skin.
void installSkinGenex(PaintCtx &ctx) {
    auto cref = [](const QColor &c) -> unsigned {
        return (unsigned)(c.red() | (c.green() << 8) | (c.blue() << 16));
    };
    auto rgb = [](int r, int g, int b) -> unsigned {
        return (unsigned)(r | (g << 8) | (b << 16));
    };
    auto col = [&](std::initializer_list<const char *> ids,
                   int dr, int dg, int db) -> unsigned {
        for (const char *id : ids) {
            QColor g = themed(ctx, id, QColor());
            if (g.isValid()) return cref(g);
        }
        return rgb(dr, dg, db);
    };
    // Index order follows the WADLG_* enum; the rgb(...) literals are
    // the default colours used when the skin defines no override.
    unsigned c[24];
    c[0]  = col({"wasabi.list.background", "color.display.bg"}, 0, 0, 0);            // ITEMBG
    c[1]  = col({"wasabi.list.text", "color.display"}, 0, 255, 0);                  // ITEMFG
    c[2]  = col({"wasabi.window.background", "color.window.bg"}, 36, 36, 60);       // WNDBG
    c[3]  = col({"wasabi.button.text", "wasabi.window.text"}, 57, 56, 66);          // BUTTONFG
    c[4]  = col({"wasabi.window.text", "wasabi.text.color"}, 255, 255, 255);        // WNDFG
    c[5]  = col({"wasabi.border.sunken"}, 132, 148, 165);                           // HILITE
    c[6]  = col({"wasabi.list.item.selected", "color.selected.active.bg"}, 0, 0, 198); // SELCOLOR
    c[7]  = col({"wasabi.list.header.background"}, 72, 72, 120);                     // LISTHEADER_BG
    c[8]  = col({"wasabi.list.header.text"}, 255, 255, 255);                         // LISTHEADER_FONT
    c[9]  = rgb(108, 108, 180);  // FRAME_TOP
    c[10] = rgb(36, 36, 60);     // FRAME_MIDDLE
    c[11] = rgb(18, 18, 30);     // FRAME_BOTTOM
    c[12] = rgb(36, 36, 60);     // EMPTY_BG
    c[13] = rgb(36, 36, 60);     // SCROLLBAR_FG
    c[14] = rgb(36, 36, 60);     // SCROLLBAR_BG
    c[15] = rgb(121, 130, 150);  // SCROLLBAR_INV_FG
    c[16] = rgb(78, 88, 110);    // SCROLLBAR_INV_BG
    c[17] = rgb(36, 36, 60);     // SCROLLBAR_DEADAREA
    c[18] = col({"wasabi.list.text.selected"}, 255, 255, 255);                      // SELBAR_FG
    c[19] = col({"wasabi.list.text.selected.background",
                 "wasabi.list.item.selected"}, 0, 0, 180);                          // SELBAR_BG
    c[20] = col({"wasabi.list.text.selected"}, 0, 255, 0);                          // INACT_SELBAR_FG
    c[21] = rgb(0, 0, 128);      // INACT_SELBAR_BG
    c[22] = c[0];                // ITEMBG2
    c[23] = c[1];                // ITEMFG2

    void *genex = qtwasabi_make_genex(c);
    qtwasabi_set_genskin_bitmap(genex);
    WADlg_init(nullptr);   // our IPC short-circuits before the hwnd check

    if (std::getenv("WASABIQT_TRACE_WADLG")) {
        static const char *nm[24] = {
            "ITEMBG","ITEMFG","WNDBG","BUTTONFG","WNDFG","HILITE","SELCOLOR",
            "LH_BG","LH_FONT","LH_TOP","LH_MID","LH_BOT","LH_EMPTY",
            "SB_FG","SB_BG","SB_INVFG","SB_INVBG","SB_DEAD",
            "SELBAR_FG","SELBAR_BG","INACT_SELFG","INACT_SELBG","ITEMBG2","ITEMFG2"};
        for (int i = 0; i < 24; ++i)
            std::fprintf(stderr, "[wadlg] %-12s = 0x%06X\n", nm[i], WADlg_getColor(i));
    }
}

QString mlIconsBaseDir() {
    if (const char *env = std::getenv("WASABIQT_ML_ICONS_DIR"))
        return QString::fromLocal8Bit(env);
    return QStringLiteral(
        "/home/snekmin/git/winamp-linux/Src/Plugins/Library");
}

// Load a BMP from the media-library resource tree and strip its
// colour-key transparency.  The tree/toolbar icons are opaque 16-colour
// BMPs blitted with Win32 TransparentBlt(): the pixel in a corner defines the
// transparent colour for that bitmap — most are black (RGB 0,0,0), the
// classic ones are magenta (RGB 255,0,255).  We key out BOTH: always
// magenta, plus the corner-consensus colour when all four corners agree
// (the exact TransparentBlt rule).  Detecting the key from the bitmap
// itself keeps this generic — every plugin's icons and every skin's art
// come out with proper alpha rather than a solid box behind the glyph.
// Cached per-process so the disk hit only happens once per icon.
QImage loadMlIcon(const QString &relPath) {
    if (relPath.isEmpty()) return {};
    static QHash<QString, QImage> cache;
    auto it = cache.constFind(relPath);
    if (it != cache.constEnd()) return it.value();
    QImage img(relPath);
    if (!img.isNull()) {
        img = img.convertToFormat(QImage::Format_ARGB32);
        const int w = img.width(), h = img.height();
        const QRgb magenta = qRgb(255, 0, 255) & 0x00FFFFFF;
        // Corner-consensus transparent key: if all four corners share one
        // colour, TransparentBlt would treat that colour as transparent.
        QRgb cornerKey = 0xFFFFFFFFu;  // sentinel = "no consensus"
        if (w >= 2 && h >= 2) {
            const QRgb tl = img.pixel(0, 0)         & 0x00FFFFFF;
            const QRgb tr = img.pixel(w - 1, 0)     & 0x00FFFFFF;
            const QRgb bl = img.pixel(0, h - 1)     & 0x00FFFFFF;
            const QRgb br = img.pixel(w - 1, h - 1) & 0x00FFFFFF;
            if (tl == tr && tl == bl && tl == br) cornerKey = tl;
        }
        for (int y = 0; y < h; ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const QRgb rgb = row[x] & 0x00FFFFFF;
                if (rgb == magenta || rgb == cornerKey) row[x] = 0u;
            }
        }
    }
    cache.insert(relPath, img);
    return img;
}

// Load + recolour one of the monochrome ML toolbar bitmaps using the
// MLIF_BUTTONBLENDPLUSCOLOR image-filter rule.  These view-mode button
// icons (icn_alb_art / icn_view_mode / icn_columns) are authored as
// BLACK art on a WHITE field: luminance becomes alpha (white→transparent,
// black→opaque) and the opaque part is tinted to the skin's
// button-foreground colour.  This keeps the icons generic: every skin
// themes them through its own foreground colour, and the dropdown
// down-triangle baked into icn_view_mode / icn_columns comes through for
// free.  Cached per (path,tint) so a colour change re-tints.
QImage loadMlToolbarIcon(const QString &relPath, QColor tint) {
    if (relPath.isEmpty()) return {};
    const QString key =
        relPath + QStringLiteral("#%1").arg(tint.rgb(), 8, 16, QLatin1Char('0'));
    static QHash<QString, QImage> cache;
    auto it = cache.constFind(key);
    if (it != cache.constEnd()) return it.value();
    QImage src(relPath);
    QImage out;
    if (!src.isNull()) {
        src = src.convertToFormat(QImage::Format_ARGB32);
        const int w = src.width(), h = src.height();
        out = QImage(w, h, QImage::Format_ARGB32);
        const int tr = tint.red(), tg = tint.green(), tb = tint.blue();
        for (int y = 0; y < h; ++y) {
            const QRgb *s = reinterpret_cast<const QRgb *>(src.scanLine(y));
            QRgb *d = reinterpret_cast<QRgb *>(out.scanLine(y));
            for (int x = 0; x < w; ++x) {
                // qGray ≈ luma; inverted luminance = coverage/alpha.
                const int a = 255 - qGray(s[x]);
                d[x] = qRgba(tr, tg, tb, a);
            }
        }
    }
    cache.insert(key, out);
    return out;
}

// Classic Wasabi/wa_dlg DCW_SUNKENBORDER: a recessed 1-px 3-D edge —
// dark on top+left, light on bottom+right — drawn around list panes,
// the tree, and the search box so each section reads as inset.
inline void drawSunkenBorder(QPainter *p, const QRect &r,
                              const QColor &lite, const QColor &dark) {
    p->setPen(dark);
    p->drawLine(r.topLeft(),  r.topRight());
    p->drawLine(r.topLeft(),  r.bottomLeft());
    p->setPen(lite);
    p->drawLine(r.bottomLeft() + QPoint(1, 0), r.bottomRight());
    p->drawLine(r.topRight()   + QPoint(0, 1), r.bottomRight());
}

// Build the default sidebar tree shown when no media-library plugin
// has registered its own entries.  Kept as a free function so a
// plugin-driven nav model can replace it without touching
// MlHostWidget's paint logic.
QList<TreeListNode> buildPluginTree() {
    auto icon = [](const char *rel) {
        return QStringLiteral("%1/%2")
            .arg(mlIconsBaseDir(),
                  QString::fromLatin1(rel));
    };
    QList<TreeListNode> roots;

    // "Now Playing" at the very top — the canonical media-library
    // nav-tree order puts it first.
    TreeListNode nowPlaying;
    nowPlaying.invariantId  = QStringLiteral("Now Playing");
    nowPlaying.displayLabel = QStringLiteral("Now Playing");
    nowPlaying.iconResource = icon("ml_nowplaying/resources/ti_nowplaying_16x16x16.bmp");
    roots.append(nowPlaying);

    TreeListNode local;
    local.invariantId = QStringLiteral("Local Media");
    local.displayLabel = QStringLiteral("Local Library");
    local.defaultExpanded = true;
    local.childProvider = []() {
        auto i = [](const char *rel) {
            return QStringLiteral("%1/%2")
                .arg(mlIconsBaseDir(),
                      QString::fromLatin1(rel));
        };
        return QList<TreeListNode>{
            {"Local Media/Audio",            "Audio",
              i("ml_local/resources/ti_audio_16x16x16.bmp"),         false, {}},
            {"Local Media/Video",            "Video",
              i("ml_local/resources/ti_video_16x16x16.bmp"),         false, {}},
            {"Local Media/Most Played",      "Most Played",
              i("ml_local/resources/ti_most_played_16x16x16.bmp"),   false, {}},
            {"Local Media/Recently Added",   "Recently Added",
              i("ml_local/resources/ti_recently_added_16x16x16.bmp"),false, {}},
            {"Local Media/Recently Played",  "Recently Played",
              i("ml_local/resources/ti_recently_played_16x16x16.bmp"),false,{}},
            {"Local Media/Never Played",     "Never Played",
              i("ml_local/resources/ti_never_played_16x16x16.bmp"),  false, {}},
            {"Local Media/Top Rated",        "Top Rated",
              i("ml_local/resources/ti_top_rated_16x16x16.bmp"),     false, {}},
        };
    };
    roots.append(local);

    TreeListNode playlists;
    playlists.invariantId = QStringLiteral("Playlists");
    playlists.displayLabel = QStringLiteral("Playlists");
    playlists.iconResource = icon("ml_playlists/resources/ti_playlist_16x16x16.bmp");
    playlists.childProvider = []() { return QList<TreeListNode>{}; };
    roots.append(playlists);

    TreeListNode online;
    online.invariantId  = QStringLiteral("Online Services");
    online.displayLabel = QStringLiteral("Online Services");
    online.childProvider = []() { return QList<TreeListNode>{}; };
    roots.append(online);

    TreeListNode devices;
    devices.invariantId  = QStringLiteral("Devices");
    devices.displayLabel = QStringLiteral("Devices");
    devices.iconResource = icon("ml_devices/resources/generic-device-16x16.png");
    devices.childProvider = []() { return QList<TreeListNode>{}; };
    roots.append(devices);

    TreeListNode podcasts;
    podcasts.invariantId  = QStringLiteral("Podcasts");
    podcasts.displayLabel = QStringLiteral("Podcast Directory");
    podcasts.iconResource = icon("ml_local/resources/ti_podcasts_16x16x16.bmp");
    podcasts.defaultExpanded = true;
    podcasts.childProvider = []() {
        return QList<TreeListNode>{
            {"Podcasts/Subscriptions", "Subscriptions", "", false, {}}
        };
    };
    roots.append(podcasts);

    TreeListNode bookmarks;
    bookmarks.invariantId  = QStringLiteral("Bookmarks");
    bookmarks.displayLabel = QStringLiteral("Bookmarks");
    bookmarks.iconResource = icon("ml_bookmarks/resources/ti_bookmarks_16x16x16.bmp");
    bookmarks.childProvider = []() { return QList<TreeListNode>{}; };
    roots.append(bookmarks);

    TreeListNode history;
    history.invariantId  = QStringLiteral("History");
    history.displayLabel = QStringLiteral("History");
    history.iconResource = icon("ml_history/resources/ti_history_items_16x16x16.bmp");
    history.childProvider = []() { return QList<TreeListNode>{}; };
    roots.append(history);

    return roots;
}

// Concrete HolderRenderer.  Composes:
//   * Outer SectionFrame (the bevel wrapping the whole ML panel)
//   * Top toolbar — search box + Clear Search button + filter icons
//   * Left column — TreeList + Library button at bottom-left
//   * Right area — Artist/Albums + Album/Year column panes (top
//     half) + Track list (bottom half), each wrapped in its own
//     SectionFrame so every section reads with its own border.
//   * Status row — Play ▼ button + "0 items" text.
class MlHostRenderer : public HolderRenderer {
public:
    MlHostRenderer() {
        // The sidebar tree pulls FROM the library window's embedded
        // TreeListWidget — that's where any `MLNavCtrl_InsertItem`
        // calls land.  Media-library plugins drive what we render here.
        //
        // When no plugin has registered entries we also seed the
        // built-in fallback tree, so launching with zero plugins
        // still shows something meaningful.  Plugin-driven entries
        // win when they exist.
        HWND hLib = qtWasabi::ml::ensureLibraryWindow();
        // Boot the statically-linked ml_* plugins so their
        // Plugin_Init can register tree items before we read the
        // nav model below.  Safe to call repeatedly — internally
        // guarded against running twice.
        qtWasabi::ml::loadBuiltinMlPlugins();
        using namespace qtWasabi::wasabi_compat;
        m_libraryHwnd = hLib;
        // Merge plugin-driven items with the built-in fallback tree.
        // Plugin items go first (so each registered plugin visibly
        // replaces its fallback equivalent at the top of the list).
        // Fallback items survive unless a plugin item already covers
        // the same name.
        QList<qtWasabi::TreeListNode> roots;
        QSet<QString> pluginNames;
        if (m_libraryHwnd) {
            auto *lib = static_cast<qtWasabi::ml::MlLibraryWindow *>(
                lookupHandle<WindowObject>(m_libraryHwnd));
            if (lib) {
                for (const auto &n : lib->nav().widget().roots()) {
                    pluginNames.insert(n.displayLabel);
                    roots.append(n);
                }
            }
        }
        for (const auto &n : buildPluginTree()) {
            if (!pluginNames.contains(n.displayLabel)) {
                roots.append(n);
            }
        }
        m_tree.setRoots(roots);
        // Use the magenta-key-stripping loader so the 16-colour
        // ml_* resource BMPs (which transparent-mask via RGB
        // 255,0,255) come out with proper alpha rather than a solid
        // box behind each glyph.
        m_tree.setBitmapResolver(&loadMlIcon);

        // The filter panes are 2-column: the name plus the next filter's
        // per-row count, right-aligned (real ml_local SimpleFilter shape).
        // Rows are filled from the Host's tag-indexed library on first
        // paint (reloadAll) and refiltered on selection; see below.
        m_artistsCol.appendColumn(QStringLiteral("Artist"),  155);
        m_artistsCol.appendColumn(QStringLiteral("Albums"),   48, 2);

        m_albumsCol.appendColumn(QStringLiteral("Album"),    155);
        m_albumsCol.appendColumn(QStringLiteral("Tracks"),    48, 2);

        m_tracks.appendColumn(QStringLiteral("Artist"),   110);
        m_tracks.appendColumn(QStringLiteral("Album"),    130);
        m_tracks.appendColumn(QStringLiteral("#"),         32, 2);
        m_tracks.appendColumn(QStringLiteral("Title"),    170);
        m_tracks.appendColumn(QStringLiteral("Length"),    54, 2);
        m_tracks.appendColumn(QStringLiteral("Genre"),     70);
        m_tracks.appendColumn(QStringLiteral("Year"),      46, 2);
    }

    bool isInteractive() const override { return true; }

    void paint(QPainter *p, PaintCtx &ctx, const QRect &r) override {
        if (r.width() <= 0 || r.height() <= 0) return;
        m_lastRect = r;

        // Populate the artist/album/track panes from the Host's
        // tag-indexed library on the first paint that has a Host.
        if (ctx.host && !m_dataLoaded) {
            m_dataLoaded = true;
            reloadAll(ctx.host);
        }

        // Prime wa_dlg with the live skin's palette once, so any
        // WADlg_* draw calls resolve to the active skin's colours.
        static bool s_genexDone = false;
        if (!s_genexDone) { s_genexDone = true; installSkinGenex(ctx); }

        // Resolve the wa_dlg theme from the loaded skin's own named
        // colours (e.g. a skin's system-colors.xml), NOT invented
        // `color.ml.*` ids that no skin defines (those would always
        // fall back to the hardcoded darks below, leaving the ML chrome
        // unthemed).  `themed()` walks the candidate list and returns
        // the first the skin defines, so this is generic: any skin's
        // own colours theme the ML.
        auto firstThemed = [&](std::initializer_list<const char *> ids,
                                QColor fallback) {
            for (const char *id : ids) {
                QColor got = themed(ctx, id, QColor());
                if (got.isValid()) return got;
            }
            return fallback;
        };
        // These map the canonical wa_dlg colour slots to the skin
        // colour-element names used to synthesise the genex.  Using the
        // standard `wasabi.*` names themes the ML correctly for ANY
        // skin, not just Bento.  The trailing `color.*` candidates are
        // Bento-private aliases kept as a backstop.
        // WADLG_WNDBG (ptr[52]) — dialog/chrome background.
        const QColor windowBg = firstThemed(
            {"wasabi.window.background", "color.window.bg"},
            QColor(51, 55, 56));
        // WADLG_ITEMBG (ptr[48]) — list/edit background.
        const QColor itemBg = firstThemed(
            {"wasabi.list.background", "color.display.bg"},
            QColor(8, 9, 10));
        // WADLG_WNDFG (ptr[56]) — chrome/window text.
        const QColor windowTxt = firstThemed(
            {"wasabi.window.text", "wasabi.text.color", "color.window.txt"},
            QColor(210, 210, 210));
        // WADLG_ITEMFG (ptr[50]) — list row text.
        const QColor itemFg = firstThemed(
            {"wasabi.list.text", "color.display"},
            QColor(147, 175, 185));
        // WADLG_HILITE (ptr[58]) — the skin's actual divider / sunken-
        // border colour.  Use it for the dark bevel edge; pair it with
        // a light edge derived from the window bg for the 3-D look.
        const QColor sunken    = firstThemed(
            {"wasabi.border.sunken"}, windowBg.darker(170));
        // WADLG_SELBAR_BG (ptr[86]) — selection bar background.
        const QColor selBg     = firstThemed(
            {"wasabi.list.text.selected.background",
             "wasabi.list.item.selected", "color.selected.active.bg"},
            QColor(49, 53, 64));
        const QColor bg        = windowBg;
        const QColor frameLite = windowBg.lighter(150);
        const QColor frameDark = sunken;
        const QColor text      = windowTxt;
        (void)itemFg; (void)selBg;

        p->save();
        p->fillRect(r, bg);

        // Outer 1-px bevel around the whole holder.
        p->setPen(frameLite);
        p->drawLine(r.topLeft(),    r.topRight());
        p->drawLine(r.topLeft(),    r.bottomLeft());
        p->setPen(frameDark);
        p->drawLine(r.bottomLeft() + QPoint(1, 0), r.bottomRight());
        p->drawLine(r.topRight(),                   r.bottomRight());

        QFont smallFont(QStringLiteral("Tahoma"));
        smallFont.setPixelSize(11);
        const QFontMetrics smallFm(smallFont);
        const int rowH      = qMax(14, smallFm.height() + 2);
        const int toolbarH  = 26;
        const int bottomH   = 24;
        const int sidebarW  = 134;
        // Button foreground (label text + view-mode glyphs), tinted to
        // the skin's button-fg (WADLG_BUTTONFG).  On Bento's dark button
        // that resolves LIGHT, so labels and icons read on the dark
        // chrome.  Falls back to the skin's window text, then a light
        // grey — never hardcoded for one skin.
        const QColor btnText = firstThemed(
            {"wasabi.button.text", "wasabi.window.text", "color.window.txt"},
            windowTxt.lightnessF() > 0.5 ? windowTxt : QColor(214, 216, 220));
        // The wa_dlg genex button is a fixed light-silver face, so its
        // labels + view-mode glyphs need a dark foreground to read (the
        // skin's button-fg is tuned for the skin's own button colour and
        // resolves LIGHT on dark skins like Bento, which would vanish on
        // silver).  Matches real Winamp's dark ML button labels.
        const QColor btnFg(56, 56, 60);

        // ── Layout geometry ────────────────────────────────────────
        // Left column = the sidebar tree, full height from the top
        // down to the bottom button row.  Right column = the Search
        // toolbar at the TOP (level with "Now Playing", NOT a
        // full-width bar above it), with the Artist/Album/Tracks panes
        // below it.  Bottom row spans full width (Library under the
        // sidebar, Play ▼ + status on the right).
        const int topY = r.y() + 1;
        const QRect bottomRow(r.x() + 1, r.bottom() - bottomH,
                               r.width() - 2, bottomH);
        const QRect sidebar(r.x() + 1, topY, sidebarW,
                             bottomRow.top() - topY - 1);
        const QRect toolbar(sidebar.right() + 2, topY,
                             r.right() - sidebar.right() - 3, toolbarH);
        p->fillRect(toolbar, bg);

        // View-mode buttons at the LEFT of the Search field: three
        // skinned buttons, each the skin's button chrome holding a
        // black (skin-foreground-tinted) view-mode glyph.  The
        // ml_local resource bitmaps are: icn_alb_art (album-art toggle,
        // NO arrow), icn_view_mode (list view) and icn_columns (details
        // view) — the latter two carry the dropdown down-triangle baked
        // into the bitmap itself.  Tinted via loadMlToolbarIcon
        // (luminance→alpha), so the icons stay generic across skins
        // (each themes through its own button foreground).
        static const char *kViewModeIcons[] = {
            "ml_local/resources/icn_alb_art.bmp",
            "ml_local/resources/icn_view_mode.bmp",
            "ml_local/resources/icn_columns.bmp",
        };
        const int vmTop = toolbar.y() + 3;
        const int vmH   = toolbar.height() - 6;
        int vmX = toolbar.x() + 4;
        for (int i = 0; i < 3; ++i) {
            const QImage ic = loadMlToolbarIcon(
                QStringLiteral("%1/%2")
                    .arg(mlIconsBaseDir(),
                          QString::fromLatin1(kViewModeIcons[i])),
                btnFg);
            const int iw = ic.isNull() ? 16 : ic.width();
            const int ih = ic.isNull() ? 11 : ic.height();
            const int bw = iw + 14;
            const QRect btn(vmX, vmTop, bw, vmH);
            drawWadlgButton(p, btn, i == m_viewMode);
            if (!ic.isNull()) {
                const int ix = btn.x() + (bw - iw) / 2;
                const int iy = btn.y() + (vmH - ih) / 2 + (i == m_viewMode ? 1 : 0);
                p->drawImage(ix, iy, ic);
            }
            m_viewBtnCanvas[i] =
                QRect(p->transform().map(btn.topLeft()), btn.size());
            vmX += bw + 3;
        }

        p->setFont(smallFont);
        p->setPen(text);
        const QRect searchLbl(vmX + 4, toolbar.y(), 50, toolbar.height());
        p->drawText(searchLbl, Qt::AlignVCenter | Qt::AlignLeft,
                    QStringLiteral("Search:"));
        const QRect searchBox(searchLbl.right() + 2, toolbar.y() + 4,
                                toolbar.right() - 96 - (searchLbl.right() + 2),
                                toolbar.height() - 8);
        p->fillRect(searchBox, itemBg);
        // Sunken edit border: dark top/left, light bottom/right.
        p->setPen(frameDark);
        p->drawLine(searchBox.topLeft(), searchBox.topRight());
        p->drawLine(searchBox.topLeft(), searchBox.bottomLeft());
        p->setPen(frameLite);
        p->drawLine(searchBox.bottomLeft(), searchBox.bottomRight());
        p->drawLine(searchBox.topRight(), searchBox.bottomRight());

        const QRect clearBtn(toolbar.right() - 92, toolbar.y() + 4,
                              88, toolbar.height() - 8);
        drawWadlgButton(p, clearBtn, false);
        p->setPen(btnFg);
        p->drawText(clearBtn, Qt::AlignCenter,
                    QStringLiteral("Clear Search"));
        m_clearBtnCanvas =
            QRect(p->transform().map(clearBtn.topLeft()), clearBtn.size());
        m_searchBoxCanvas =
            QRect(p->transform().map(searchBox.topLeft()), searchBox.size());

        // ── Bottom row with Library + Play ▼ side by side ──────
        p->fillRect(bottomRow, bg);
        p->setPen(frameLite);
        p->drawLine(bottomRow.topLeft(), bottomRow.topRight());

        const QRect libBtn(bottomRow.x() + 4, bottomRow.y() + 3,
                             sidebarW - 8, bottomRow.height() - 6);
        drawWadlgButton(p, libBtn, false);
        p->setPen(btnFg);
        p->drawText(libBtn, Qt::AlignCenter,
                    QStringLiteral("Library"));
        m_libBtnCanvas =
            QRect(p->transform().map(libBtn.topLeft()), libBtn.size());

        // gen_ml's play control is a SPLIT button: the "Play" face runs
        // the default action, the separated arrow segment opens the
        // action menu (Play / Enqueue), divider line between them.
        const QRect playBtn(libBtn.right() + 8, bottomRow.y() + 3,
                             60, bottomRow.height() - 6);
        drawWadlgButton(p, playBtn, false);
        const int arrowW = 16;
        const QRect playFace(playBtn.x(), playBtn.y(),
                              playBtn.width() - arrowW, playBtn.height());
        const QRect arrowSeg(playFace.right() + 1, playBtn.y(),
                              arrowW, playBtn.height());
        p->setPen(frameLite);
        p->drawLine(arrowSeg.topLeft(), arrowSeg.bottomLeft());
        p->setPen(btnFg);
        p->drawText(playFace, Qt::AlignCenter, QStringLiteral("Play"));
        {   // 7x4 down arrow, centred in the segment (wa_dlg style)
            const int ax = arrowSeg.center().x() - 3;
            const int ay = arrowSeg.center().y() - 2;
            for (int i = 0; i < 4; ++i)
                p->drawLine(ax + i, ay + i, ax + 6 - i, ay + i);
        }
        // Canvas-space rects for click routing (the holder paints us
        // through a translated painter, so map to the click coord space).
        m_playBtnCanvas = QRect(p->transform().map(playFace.topLeft()),
                                playFace.size());
        m_playMenuCanvas = QRect(p->transform().map(arrowSeg.topLeft()),
                                 arrowSeg.size());

        p->setPen(text);
        p->drawText(QRect(playBtn.right() + 12, bottomRow.y(),
                           bottomRow.width() - 200, bottomRow.height()),
                    Qt::AlignVCenter | Qt::AlignLeft,
                    m_statusText);

        // ── Left sidebar (tree) — full height from the top ─────
        // Sunken bevel frame around the sidebar pane.
        drawSunkenBorder(p, sidebar, frameLite, frameDark);
        m_tree.attrs.insert(QStringLiteral("x"),
                             QString::number(sidebar.x() + 1));
        m_tree.attrs.insert(QStringLiteral("y"),
                             QString::number(sidebar.y() + 1));
        m_tree.attrs.insert(QStringLiteral("w"),
                             QString::number(sidebar.width() - 2));
        m_tree.attrs.insert(QStringLiteral("h"),
                             QString::number(sidebar.height() - 2));
        m_tree.paint(p, ctx, QSize(r.width() + r.x(), r.height() + r.y()));

        // ── Right area: Artist + Album top panes, Tracks bottom ──
        // Sits BELOW the search toolbar (which occupies the top of the
        // right column), so the panes line up under the Search field.
        const QRect rightArea(toolbar.x(),
                                toolbar.bottom() + 2,
                                toolbar.width(),
                                bottomRow.top() - toolbar.bottom() - 3);
        const int paneH = (rightArea.height() - 2) / 2;
        const int leftPaneW = rightArea.width() / 2;
        const QRect artistPane(rightArea.x(),
                                rightArea.y(),
                                leftPaneW,
                                paneH);
        const QRect albumPane (rightArea.x() + leftPaneW + 2,
                                rightArea.y(),
                                rightArea.width() - leftPaneW - 2,
                                paneH);
        const QRect tracksPane(rightArea.x(),
                                rightArea.y() + paneH + 2,
                                rightArea.width(),
                                rightArea.height() - paneH - 2);

        auto paintBorderedPane = [&](MultiColumnListWidget &mcl,
                                       const QRect &paneR) {
            // Sunken bevel frame around the pane.
            drawSunkenBorder(p, paneR, frameLite, frameDark);
            // Stamp the widget's rect to the inner content area.
            const QRect inner = paneR.adjusted(1, 1, -1, -1);
            mcl.attrs.insert(QStringLiteral("x"),
                              QString::number(inner.x()));
            mcl.attrs.insert(QStringLiteral("y"),
                              QString::number(inner.y()));
            mcl.attrs.insert(QStringLiteral("w"),
                              QString::number(inner.width()));
            mcl.attrs.insert(QStringLiteral("h"),
                              QString::number(inner.height()));
            mcl.paint(p, ctx, QSize(r.right() + 1, r.bottom() + 1));
        };
        m_artistsCol.setActive(m_activePane == 0);
        m_albumsCol.setActive(m_activePane == 1);
        m_tracks.setActive(m_activePane == 2);
        paintBorderedPane(m_artistsCol, artistPane);
        paintBorderedPane(m_albumsCol,  albumPane);
        paintBorderedPane(m_tracks,     tracksPane);

        p->restore();

        // Debug: dump the rendered ML region to a PNG for visual iteration
        // (the holder itself is behind a hidden tab in Bento).
        if (const char *dp = std::getenv("WASABIQT_DUMP_ML")) {
            QPaintDevice *dev = p->device();
            if (dev && dev->devType() == QInternal::Image)
                static_cast<QImage *>(dev)->copy(r).save(
                    QString::fromLocal8Bit(dp));
        }
    }

    void onLeftButtonDown(QPoint pos, PaintCtx &ctx) override {
        // View-mode buttons (album-art / list / details) — select the mode.
        for (int i = 0; i < 3; ++i)
            if (m_viewBtnCanvas[i].contains(pos)) { m_viewMode = i; return; }
        // Clear Search / Library both reset the view to the whole library.
        if (ctx.host && (m_clearBtnCanvas.contains(pos) ||
                         m_libBtnCanvas.contains(pos))) {
            m_searchText.clear();
            m_activePane = 0;
            reloadAll(ctx.host);
            return;
        }
        // Play split button: the face plays the current track view from
        // the selected row; the arrow segment opens the action menu
        // (Play / Enqueue), like gen_ml's TrackPopupMenu.
        if (ctx.host && m_playBtnCanvas.contains(pos)) {
            playVisible(ctx.host,
                        qMax(0, m_tracks.selection()), false);
            return;
        }
        if (ctx.host && m_playMenuCanvas.contains(pos)) {
            QMenu menu;
            QAction *play = menu.addAction(QStringLiteral("Play"));
            QAction *enq  = menu.addAction(QStringLiteral("Enqueue"));
            Host *host = ctx.host;
            const int row = qMax(0, m_tracks.selection());
            QAction *picked = menu.exec(QCursor::pos());
            if (picked == play)     playVisible(host, row, false);
            else if (picked == enq) playVisible(host, row, true);
            return;
        }
        // Route into whichever child widget's rect contains pos.  Clicking
        // a nav-tree node selects it and resets the panes to the whole
        // library (the built-in "Local Library" sections all map to "all"
        // for now).
        if (m_tree.lastCanvasRect.contains(pos)) {
            m_tree.onLeftButtonDown(pos, ctx);
            if (ctx.host) { m_activePane = 0; reloadAll(ctx.host); }
            return;
        }
        m_artistsCol.onLeftButtonDown(pos, ctx);
        m_albumsCol.onLeftButtonDown(pos, ctx);
        m_tracks.onLeftButtonDown(pos, ctx);
        if (!ctx.host) return;
        // Selecting an artist refilters the albums + tracks; selecting an
        // album refilters the tracks.  Row 0 of each pane is the "All"
        // summary → an empty filter.
        if (m_artistsCol.selection() != m_lastArtistSel) {
            m_lastArtistSel = m_artistsCol.selection();
            m_activePane = 0;
            const int i = m_lastArtistSel;
            m_selArtist = (i > 0 && i - 1 < m_artists.size())
                              ? m_artists[i - 1].name : QString();
            refreshAlbums(ctx.host);
            refreshTracks(ctx.host);
        } else if (m_albumsCol.selection() != m_lastAlbumSel) {
            m_lastAlbumSel = m_albumsCol.selection();
            m_activePane = 1;
            const int j = m_lastAlbumSel;
            m_selAlbum = (j > 0 && j - 1 < m_albums.size())
                             ? m_albums[j - 1].name : QString();
            refreshTracks(ctx.host);
        } else if (m_tracks.lastCanvasRect().contains(pos)) {
            m_activePane = 2;   // clicked in the track grid
            // Double-click a track row → play the view from that track.
            const int row = m_tracks.selection();
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (row >= 0 && row == m_lastTrackClickRow &&
                now - m_lastTrackClickMs < 400) {
                playVisible(ctx.host, row, false);
                m_lastTrackClickMs = 0;
                m_lastTrackClickRow = -1;
            } else {
                m_lastTrackClickMs = now;
                m_lastTrackClickRow = row;
            }
        }
    }

private:
    // ── Media Library data plumbing ─────────────────────────────────
    // Pull the whole artist → album → track view from the Host on the
    // first paint, then refilter incrementally on selection.
    void reloadAll(Host *host) {
        m_selArtist.clear();
        m_selAlbum.clear();
        refreshArtists(host);
        refreshAlbums(host);
        refreshTracks(host);
    }
    void refreshArtists(Host *host) {
        m_artists = host->mlArtists();
        const int totalAlbums = host->mlAlbums(QString()).size();
        m_artistsCol.clearRows();
        m_artistsCol.appendRow({allSummary(m_artists.size(), "artist"),
                                 QString::number(totalAlbums)});
        for (const auto &a : m_artists)
            m_artistsCol.appendRow({a.name, QString::number(a.albumCount)});
        m_artistsCol.setSelection(0);
        m_lastArtistSel = 0;
    }
    void refreshAlbums(Host *host) {
        m_albums = host->mlAlbums(m_selArtist);
        m_selAlbum.clear();
        int totalTracks = 0;
        for (const auto &a : m_albums) totalTracks += a.trackCount;
        m_albumsCol.clearRows();
        m_albumsCol.appendRow({allSummary(m_albums.size(), "album"),
                                QString::number(totalTracks)});
        for (const auto &a : m_albums)
            m_albumsCol.appendRow({a.name, QString::number(a.trackCount)});
        m_albumsCol.setSelection(0);
        m_lastAlbumSel = 0;
    }
    void refreshTracks(Host *host) {
        m_trackRows = host->mlTracks(m_selArtist, m_selAlbum);
        m_tracks.clearRows();
        qint64 totalMs = 0;
        for (const auto &t : m_trackRows) {
            totalMs += t.lengthMs;
            m_tracks.appendRow({
                t.artist, t.album,
                t.track > 0 ? QString::number(t.track) : QString(),
                t.title, formatLen(t.lengthMs), t.genre,
                t.year > 0 ? QString::number(t.year) : QString()});
        }
        m_statusText = m_trackRows.isEmpty()
            ? QStringLiteral("0 items")
            : QStringLiteral("%1 items  [%2]").arg(m_trackRows.size())
                  .arg(formatLen(totalMs));
    }
    // Send the current track view to the player, playing from `startRow`.
    void playVisible(Host *host, int startRow, bool enqueueOnly) {
        if (!host || m_trackRows.isEmpty()) return;
        QList<QString> paths;
        paths.reserve(m_trackRows.size());
        for (const auto &t : m_trackRows) paths.append(t.path);
        host->mlPlayTracks(paths, startRow, enqueueOnly);
    }
    static QString allSummary(int n, const char *noun) {
        return QStringLiteral("All (%1 %2%3)")
            .arg(n).arg(QString::fromLatin1(noun)).arg(n == 1 ? "" : "s");
    }
    static QString formatLen(qint64 ms) {
        if (ms <= 0) return QString();
        const qint64 s = ms / 1000;
        return QStringLiteral("%1:%2").arg(s / 60)
            .arg(s % 60, 2, 10, QLatin1Char('0'));
    }

    TreeListWidget        m_tree;
    MultiColumnListWidget m_artistsCol;
    MultiColumnListWidget m_albumsCol;
    MultiColumnListWidget m_tracks;
    QRect                 m_lastRect;
    // Handle to the media-library window — set on construction.  Read
    // once to seed the nav tree; a tree-mutation signal could later
    // drive incremental repaints on plugin-driven inserts.
    HWND                  m_libraryHwnd = nullptr;

    // Live Media Library state, sourced from the Host on first paint.
    bool                        m_dataLoaded = false;
    QList<Host::MlArtistRow>    m_artists;
    QList<Host::MlAlbumRow>     m_albums;
    QList<Host::MlTrackRow>     m_trackRows;    // current track view (with paths)
    QString                     m_selArtist;    // "" = all artists
    QString                     m_selAlbum;     // "" = all albums
    int                         m_lastArtistSel = -1;
    int                         m_lastAlbumSel  = -1;
    int                         m_activePane    = 0;   // 0 artist,1 album,2 track
    QString                     m_statusText = QStringLiteral("0 items");
    QRect                       m_playBtnCanvas;       // Play face hit rect
    QRect                       m_playMenuCanvas;      // arrow segment hit rect
    QRect                       m_viewBtnCanvas[3];    // view-mode buttons
    QRect                       m_clearBtnCanvas;      // Clear Search
    QRect                       m_libBtnCanvas;        // Library
    QRect                       m_searchBoxCanvas;     // search edit box
    int                         m_viewMode   = 1;      // 0 art,1 list,2 details
    QString                     m_searchText;          // live search filter
    int                         m_lastTrackClickRow = -1;
    qint64                      m_lastTrackClickMs  = 0;
};

}  // anonymous

void installMlHostFactory() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    registerHolderRenderer(
        QStringLiteral("{6B0EDF80-C9A5-11D3-9F26-00C04F39FFC6}"),
        [](const QRect &) -> std::unique_ptr<HolderRenderer> {
            return std::make_unique<MlHostRenderer>();
        });
}

}  // namespace ml
}  // namespace qtWasabi
