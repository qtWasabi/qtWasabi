#pragma once
//
// <groupxfade> — a container whose children represent alternative
// "pages" of content, cross-faded between by `_active_page` mutation
// at runtime.  Today the renderer treats GroupXFade exactly like a
// plain Container: every child's `visible=` attribute drives whether
// it paints, and the embedder is responsible for setting `visible=0`
// on inactive pages.  A real Wasabi-style cross-fade alpha curve is
// future work — kept as its own subclass so that work lands in one
// file without disturbing every other Container consumer.
//

#include "Container.h"

namespace qtWasabi {

class GroupXFadeWidget : public ContainerWidget {};

}  // namespace qtWasabi
