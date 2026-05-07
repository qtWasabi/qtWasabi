#pragma once
//
// LayerPainter — minimal `<layer>` widget renderer.  Given a
// WasabiQt::BitmapRegistry and a layer's resolved attributes,
// blits the referenced bitmap onto a QPainter at the requested
// position and size.
//
// This is the smallest paint primitive in Wasabi — `<layer>` is
// the bitmap-blitting widget.  `<button>` and others build on it.

#include <QtCore/qglobal.h>
#include <QHash>
#include <QString>

class QPainter;
class QRect;
class QSize;

namespace WasabiQt {

class BitmapRegistry;

namespace LayerPainter {

// Paint a <layer> with the given attributes onto `p`, given the
// containing widget's pixel size (for `relatw`/`relath` resolution).
//
// Recognised attributes:
//   image        — bitmap id in the registry
//   x, y         — top-left position in pixels
//   w, h         — explicit size; 0 means "natural size of the bitmap"
//   relatw=1     — w/h are relative to container size minus this much
//                  (`w=-12 relatw=1` ⇒ container.width() - 12)
//   relath=1     — same for height
//
// Anything else is ignored at this level (the widget tree handles
// id, ghost, etc.).  Returns true if a bitmap was found and painted.
bool paintLayer(QPainter *p, BitmapRegistry &reg,
                const QHash<QString, QString> &attrs,
                const QSize &containerSize);

}  // namespace LayerPainter
}  // namespace WasabiQt
