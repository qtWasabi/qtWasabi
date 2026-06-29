#pragma once
//
// <milkdrop> — placeholder slot for an OpenGL-rendered MilkDrop
// visualizer.  The widget itself paints nothing; it exists only to
// reserve a rect in the layout and record its canvas-space bbox via
// the inherited `Widget::lastCanvasRect`.  The embedder (qtamp) reads
// that rect each frame and snaps a `QQuickFramebufferObject` overlay
// to it, where libprojectM does the actual GL rendering.
//
// Why a placeholder and not an in-tree paint?  qtWasabi's whole paint
// pipeline is CPU-based (QPainter into a QImage uploaded as a
// QSGTexture).  MilkDrop demands an OpenGL context; retrofitting GL
// through every Widget for one visualizer would mean reshaping the
// engine.  The placeholder + overlay pattern keeps GL contained to
// the one place that needs it.
//
// `Widget::lastPaintedAtMs` (inherited from the base class) is
// bumped from `paint()` on every render pass.  When the parent
// group is hidden (the AVS drawer is closed), paint() stops running
// and the timestamp goes stale; the embedder treats that as "hide
// the overlay" so the GL context idles instead of rendering
// invisible frames.  The same liveness mechanism is also used by
// `WindowHolderWidget` for the canonical Wasabi AVS slot
// (`<windowholder hold="guid:avs">`) so existing skins that
// declare an AVS slot the standard way get MilkDrop without any
// XML changes.
//

#include <qtWasabi/Widget.h>

namespace qtWasabi {

class MilkdropWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
    // Not clickable.  The overlay sits on top in z-order so any input
    // routing is the GL surface's problem, not the placeholder's.
    bool isInteractive() const override { return false; }
};

}  // namespace qtWasabi
