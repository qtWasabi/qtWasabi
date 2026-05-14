#pragma once
//
// <xmlrenderer> — sub-XML injection point.  Real Wasabi inlines a referenced XML fragment at expansion time; today we register so the factory recognises the tag.  Paint is a no-op (children recurse).
//
// Phase 5 placeholder: registers the tag with the factory but
// inherits Widget's default paint (visibility-and-recurse).  No
// per-tag paint logic yet — that's part of Phase 6's per-instance-
// state work and the host-integration milestones that follow.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class XmlRendererWidget : public Widget {
};

}  // namespace WasabiQt
