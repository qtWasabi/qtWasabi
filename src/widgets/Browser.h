#pragma once
//
// <Browser> — embedded web browser.  Real Wasabi hosts an OS
// browser control (Win32 WebBrowser ActiveX).  Until a Qt
// WebEngine integration lands, paint a dark panel + a centered
// "Web" label so the host's intended bounds are visible.
//

#include <qtWasabi/Widget.h>

namespace qtWasabi {

class BrowserWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace qtWasabi
