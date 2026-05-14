#pragma once
//
// <scrollbar> — 3-piece track (top/middle/bottom) + thumb.  Real Wasabi reads viewport / content / position and draws the thumb at the corresponding y; today we register so the factory recognises the tag.  Phase 5 paint is a no-op (children recurse).
//
// Phase 5 placeholder: registers the tag with the factory but
// inherits Widget's default paint (visibility-and-recurse).  No
// per-tag paint logic yet — that's part of Phase 6's per-instance-
// state work and the host-integration milestones that follow.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class ScrollBarWidget : public Widget {
};

}  // namespace WasabiQt
