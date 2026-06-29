#pragma once
//
// Container — base class for the family of widget tags whose paint
// responsibility is "translate / clip / scroll the QPainter into a
// child coordinate frame and recurse into children".  Wasabi calls
// these `Group`, `Layout`, `Container`, `Groupdef`, and the XUI-
// mangled `<Wasabi:Foo>` tags that parse as `wasabi_foo`.  All five
// share identical paint semantics and differ only in lifecycle:
//
//   • <layout>  is the root for an `<container>` block; it doesn't
//     translate (root canvas IS its canvas) and doesn't collapse.
//   • <groupdef> is a template instanced by `<group id="..."/>` —
//     after expansion both render the same way.
//   • <group> / <container> are the everyday composition primitives.
//   • <wasabi_…> tags are the inlined form of an XUI groupdef ref
//     (`<Wasabi:Foo>` → groupdef xuitag="Wasabi:Foo") and behave
//     identically to <group> once expanded.
//
// ComponentBucket (the scrolling list-style container) and
// GroupXFade (the page-swapping wrapper) inherit from this class
// and override the small differences.  See the corresponding
// headers for those subclasses.
//

#include <qtWasabi/Widget.h>

#include <QPoint>
#include <QString>

namespace qtWasabi {

class ContainerWidget : public Widget {
public:
    ~ContainerWidget() override;
    // Reads `_tab_key` / `_tab_value` planted by `Layout::wireTabs`
    // (containers that are tab content pages — `wdh.X` in Bento)
    // and installs a CfgAttribStore subscription whose callback
    // flips `attrs["visible"]` to "1" iff store.get(key) ==
    // m_tabValue.  No-op when the attrs aren't present.
    void onAttrsInitialized() override;

    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;

    // Containers translate their children into a local coord space
    // and don't claim hits themselves — let the topmost child claim.
    bool isContainer() const override { return true; }

    // Override Widget::hitTest to apply the same bounds-clip that
    // paint() uses: containers that declared an explicit size reject
    // hits outside that rect, so scrolled-off bucket entries can't
    // intercept clicks on widgets below the bucket.
    Widget *hitTest(QPoint point, QPoint origin, const QSize &canvas,
                    HitCtx &ctx, QRect *outBbox) override;

protected:
    // Subclasses override to add a paint-time scroll offset applied
    // inside the container's clip rect.  Default = no scroll.
    // ComponentBucket overrides this to apply `_scroll * _entry_step`.
    virtual QPoint containerScrollOffset() const { return QPoint(0, 0); }

    // Tab subscription state, populated when `_tab_key` was planted.
    QString m_tabKey;
    int     m_tabValue     = -1;
    int     m_tabSubHandle = 0;
};

}  // namespace qtWasabi
