#pragma once
//
// <Grid> — 3-slice horizontally-stretchable bitmap chrome.  Used by
// the drawer's tab buttons (Equalizer / Options / Color Themes) and
// other Wasabi UI elements.  `left=` and `right=` are fixed-width
// endcaps; `middle=` tiles between them to fill the gap.
//

#include <QString>
#include <qtWasabi/Widget.h>

namespace qtWasabi {

class GridWidget : public Widget {
public:
    ~GridWidget() override;
    // Reads `_tab_state_*` attrs planted by Layout::wireTabs and
    // subscribes to the tab-active CfgAttribStore key.  When the
    // value matches our planted index, set visible according to
    // `_tab_show_when` (active|inactive).  Drives the Bento tab
    // pill swap (active pill highlighted, inactive pill darker)
    // without per-skin Maki dispatch.
    void onAttrsInitialized() override;
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;

private:
    int     m_tabStateSubHandle = 0;
    int     m_tabStateValue     = -1;
    QString m_tabStateKey;
    QString m_tabShowWhen;
};

}  // namespace qtWasabi
