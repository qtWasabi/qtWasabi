#pragma once
//
// MlHostWidget — visual host for the canonical Wasabi
// Media Library windowholder GUID `{6B0EDF80-…}`.
//
// Renders the media-library panel layout: bevelled section frames
// around each pane, a hierarchical navigation tree on the left,
// three multi-column list panes on the right (Artist/Albums/Tracks
// + Album/Year/Tracks + final track grid), a Library button at the
// bottom-left of the sidebar column, and a Play + status row across
// the bottom.
//
// Implemented as a HolderRenderer (see
// `qtWasabi/WindowHolderRegistry.h`) so it slots into the
// pluggable windowholder dispatch.  qtamp's bootstrap registers a
// factory at startup; a media-library plugin can supersede it by
// registering its own factory under the same GUID.
//
// All chrome and content paint into the SAME QImage the rest of
// the skin paints into, so the media-library UI composites with the
// skin's chrome without any QQuickWidget / QWidget interop.
//

#include <qtWasabi/WindowHolderRegistry.h>

#include <QList>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QString>

#include <functional>

namespace qtWasabi {
namespace ml {

// One-time installer the embedder calls at startup to register
// MlHostWidget as the default ML GUID renderer.  Idempotent.
void installMlHostFactory();

}  // namespace ml
}  // namespace qtWasabi
