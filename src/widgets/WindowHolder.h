#pragma once
//
// <windowholder> / <wmh> — host slot for an embedded sub-window
// (video output, AVS frame, browser pane, …).  qtWasabi doesn't yet
// host the embedded surfaces, so this widget paints a black
// placeholder rect that visually matches what real Winamp shows
// while the embedded component is loading or unavailable.
//

#include <QDateTime>
#include <QHash>
#include <memory>
#include <qtWasabi/Widget.h>

namespace qtWasabi {

class HolderRenderer;
class MediaLibraryPanel;

class WindowHolderWidget : public Widget {
public:
    WindowHolderWidget();
    ~WindowHolderWidget() override;
    // Holders that embed Playlist Editor / Media Library GUIDs are
    // interactive — clicks select rows.  AVS / video / unknown
    // holders are non-interactive.  Reported per-instance based on
    // the resolved `hold=` attr.
    bool isInteractive() const override;

    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
    void onLeftButtonDown(QPoint pos, PaintCtx &ctx) override;
    void onMouseMove(QPoint pos, PaintCtx &ctx) override;
    void onLeftButtonUp(QPoint pos, PaintCtx &ctx) override;
    void onMouseWheel(QPoint pos, int steps, PaintCtx &ctx) override;
    // Engine-rendered playlist/library content needs press→move→release
    // capture (scrollbar drag, row hit-test).  Without this the SkinView
    // routes the press to Maki onLeftClick/onAction only — never to the
    // renderer — so the list/scrollbar gets no events at all.  Scoped to
    // list holders so vis/video content still drags the window.
    bool capturesMouse() const override;
    // Playlist / library holders are solid interactive rectangles even
    // though they paint "list-only" (transparent between rows) — hit them
    // by bbox so a click in a row gap still lands.  AVS / video holders
    // are NOT solid (they stay alpha-transparent so the cover/overlay
    // shows through and clicks pass to the chrome beneath).
    bool isSolidHitRegion() const override;

private:
    // Per-holder scroll state for engine-rendered playlist / library
    // content.  Kept here (not in PaintCtx) so each embedded GUID
    // remembers its scroll independently.
    int    m_topRow         = 0;
    int    m_lastRowH       = 12;
    QRect  m_lastListRect;
    qint64 m_lastClickMs    = 0;
    int    m_lastClickRow   = -1;
    QHash<QString, bool> m_libraryExpansion;
    std::unique_ptr<MediaLibraryPanel> m_mlPanel;

    // Per-instance HolderRenderer once a registry factory has
    // claimed our GUID.  Created lazily on the first paint (the
    // factory receives the resolved rect for sizing-dependent
    // setup).  When non-null, the GUID's registered renderer
    // owns paint + mouse routing; the legacy AVS/Video/Playlist/
    // Library branches below remain as fallback for GUIDs nobody
    // registers.
    std::unique_ptr<HolderRenderer> m_renderer;
    bool                            m_rendererTried = false;
};

}  // namespace qtWasabi
