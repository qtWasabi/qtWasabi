#pragma once
//
// <menu> — menu-bar item with hover/down/normal states.  Real Wasabi paints the appropriate bitmap based on mouse state; today we just register so the factory recognises the tag (61 skin files reference it).  Phase 6 will add the state machine.
//
// Phase 5 placeholder: registers the tag with the factory but
// inherits Widget's default paint (visibility-and-recurse).  No
// per-tag paint logic yet — that's part of Phase 6's per-instance-
// state work and the host-integration milestones that follow.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class MenuWidget : public Widget {
};

}  // namespace WasabiQt
