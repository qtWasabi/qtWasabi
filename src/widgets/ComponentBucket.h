#pragma once
//
// <componentbucket> — a scrollable container of equally-spaced entry
// rows.  In real Wasabi, ComponentBucket holds an arbitrary list of
// child widgets and scrolls them vertically (or horizontally with
// `vertical="0"`) via the `_scroll` index, which is mutated by
// `cb_prevpage` / `cb_nextpage` button clicks.  Each child was
// materialised at `y = i * entry_step` at layout-expansion time;
// shifting the whole bucket's translate by `-scroll * step` moves
// the visible window of entries.
//

#include "Container.h"

namespace qtWasabi {

class ComponentBucketWidget : public ContainerWidget {
public:
    // Hit-test applies the same scroll offset paint() uses so a click
    // at the visible position of a scrolled-into-view entry hits the
    // right underlying widget (and not the entry that USED to be at
    // that screen position pre-scroll).
    QPoint childOriginAdjustment() const override;

    // Capture `_scroll` / `_entry_step` mutations into typed members.
    // setXmlParam still writes the value to `attrs` (via base impl)
    // so getXmlParam keeps returning what was written; the typed
    // shadow exists so paint / hit-test can read an int instead of
    // re-parsing the QString on every frame.
    void setXmlParam(const QString &name, const QString &value) override;

protected:
    QPoint containerScrollOffset() const override;

private:
    int m_scroll = 0;
    int m_entryStep = 0;
};

}  // namespace qtWasabi
