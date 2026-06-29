#pragma once
//
// LayerPainter — minimal `<layer>` widget renderer.  Given a
// qtWasabi::BitmapRegistry and a layer's resolved attributes,
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

namespace qtWasabi {

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
// `windowActive` selects the active/inactive alpha for layers that ship
// `activeAlpha`/`inactiveAlpha` (Wasabi focus convention): when true the
// layer is blitted at its `activeAlpha` (default 255), when false at its
// `inactiveAlpha` (default 255); an effective 0 skips the layer.  Hosts
// pass false for non-focused windows.  Default true preserves callers
// that don't track focus (e.g. the static Layout.cpp pass).
bool paintLayer(QPainter *p, BitmapRegistry &reg,
                const QHash<QString, QString> &attrs,
                const QSize &containerSize, bool windowActive = true);

// Same as `paintLayer` but never tiles or stretches.  When the
// destination rect is larger than the source bitmap, draws the
// bitmap at its NATURAL size positioned at the rect's top-left
// (no repeats, no scaling).  Used by `<button>` widgets — Wasabi
// treats button bitmaps as a single sprite, not as a tileable
// chrome strip.  Without this, a button declared `w=20` with a
// `15×13` icon bitmap (canonical sysmenu) renders the icon once
// plus a 5-px partial second copy ("1.5 icons" visual bug).
bool paintLayerAtNaturalSize(QPainter *p, BitmapRegistry &reg,
                               const QHash<QString, QString> &attrs,
                               const QSize &containerSize,
                               bool windowActive = true);

}  // namespace LayerPainter
}  // namespace qtWasabi
