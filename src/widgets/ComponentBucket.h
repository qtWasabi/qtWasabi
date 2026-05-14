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

namespace WasabiQt {

class ComponentBucketWidget : public ContainerWidget {
protected:
    QPoint containerScrollOffset() const override;
};

}  // namespace WasabiQt
