#pragma once
//
// <dropdownlist> — combo with edit.  Real Wasabi paints the closed-state arrow button + the active selection text; today we register so the factory recognises the tag.
//
// Phase 5 placeholder: registers the tag with the factory but
// inherits Widget's default paint (visibility-and-recurse).  No
// per-tag paint logic yet — that's part of Phase 6's per-instance-
// state work and the host-integration milestones that follow.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class DropDownListWidget : public Widget {
};

}  // namespace WasabiQt
