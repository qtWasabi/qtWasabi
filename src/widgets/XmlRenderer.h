#pragma once
//
// <xmlrenderer> — sub-XML injection point.  In the Wasabi convention
// this tag inlines a referenced XML fragment at expansion time; we
// register the tag so the factory recognises it.  The widget itself
// has no paint of its own — it inherits Widget's default
// visibility-and-recurse paint so its expanded children draw normally.
//

#include <qtWasabi/Widget.h>

namespace qtWasabi {

class XmlRendererWidget : public Widget {
};

}  // namespace qtWasabi
