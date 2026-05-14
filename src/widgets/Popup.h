#pragma once
//
// <popup> — z-overlay surface (typically transparent, holds popup contents).  Real Wasabi positions the popup at its source widget's bottom-edge; today we register so the factory recognises the tag.  Phase 5 paint is a no-op.
//
// Phase 5 placeholder: registers the tag with the factory but
// inherits Widget's default paint (visibility-and-recurse).  No
// per-tag paint logic yet — that's part of Phase 6's per-instance-
// state work and the host-integration milestones that follow.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class PopupWidget : public Widget {
};

}  // namespace WasabiQt
