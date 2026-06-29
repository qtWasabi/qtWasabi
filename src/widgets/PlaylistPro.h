#pragma once
//
// <PlaylistPro> — embedded playlist editor used by Bento / modern
// skins.  Renders rows pulled from `qtWasabi::Host::playlist*()`,
// with the canonical Wasabi look: dark blue-gray background, white
// "NN. text" lines, right-aligned m:ss duration, accent-highlighted
// current row.  Click select; double-click play.
//
// <PlaylistDirectory> — embedded Media Library / file-browser tree.
// Mirrors the same row-render pattern against
// `qtWasabi::Host::library*()`, with optional expand/collapse on
// directory rows.
//
// The same renderers are also picked up by `WindowHolderWidget`
// when its `hold=` attribute names the canonical Wasabi GUIDs
//   Playlist Editor   {45F3F7C1-A6F3-4ee6-A15E-125E92FC3F8D}
//   Media Library     {6B0EDF80-C9A5-11D3-9F26-00C04F39FFC6}
// so any skin that embeds the component by GUID renders the same
// content without per-skin glue.
//

#include <QHash>
#include <QRect>
#include <qtWasabi/Widget.h>

namespace qtWasabi {

// Free helpers used by both the dedicated widgets and WindowHolder's
// GUID-bridge.  They paint into `rect` (canvas coords) using
// `ctx.host` as the data source.  `topRow` and `currentRow` are
// in/out for the caller to persist between frames.
void paintPlaylistRows(QPainter *p, PaintCtx &ctx,
                        const QRect &rect, int &topRow);

void paintLibraryRows(QPainter *p, PaintCtx &ctx,
                       const QRect &rect, int &topRow,
                       const QString &parentPath,
                       QHash<QString, bool> &expansion);

class PlaylistProWidget : public Widget {
public:
    bool isInteractive() const override { return true; }
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;

    void onLeftButtonDown(QPoint pos, PaintCtx &ctx) override;

private:
    int    m_topRow = 0;
    int    m_lastRowH = 12;
    QRect  m_lastListRect;     // canvas coords
    qint64 m_lastClickMs = 0;
    int    m_lastClickRow = -1;
};

class PlaylistDirectoryWidget : public Widget {
public:
    bool isInteractive() const override { return true; }
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;

    void onLeftButtonDown(QPoint pos, PaintCtx &ctx) override;

private:
    int    m_topRow = 0;
    int    m_lastRowH = 12;
    QRect  m_lastListRect;
    QString m_parentPath;
    QHash<QString, bool> m_expansion;   // path → expanded
};

}  // namespace qtWasabi
