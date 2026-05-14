#pragma once
//
// <eqvis> — 31-band + preamp eq-visualisation widget.  Real Wasabi renders the live EQ curve over a graph background; today we register so the factory recognises the tag.  Phase 5 paint is a no-op (children recurse via base Widget).
//
// Phase 5 placeholder: registers the tag with the factory but
// inherits Widget's default paint (visibility-and-recurse).  No
// per-tag paint logic yet — that's part of Phase 6's per-instance-
// state work and the host-integration milestones that follow.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class EqVisWidget : public Widget {
};

}  // namespace WasabiQt
