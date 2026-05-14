#pragma once
//
// Button family — `<button>`, `<togglebutton>`, `<nstatesbutton>`.
// All three are bitmap layers with a clickable bbox; they differ in
// their state model:
//
//   • Button — single state.  Paints the `image=` bitmap (normal).
//     Pressed / hover variants exist (`downImage=`, `hoverImage=`)
//     but are input-time concerns the embedder swaps in.
//   • ToggleButton — binary on/off state held by the host.
//   • NStatesButton — multi-state (e.g. Repeat: off/all/one).  Real
//     Wasabi cycles through `nstates` and suffixes `image` with the
//     current state index — `image="player.songinfo.repeat"` →
//     actual bitmap ids `repeat0`, `repeat1`, `repeat2`.  Until
//     SkinRuntime drives the current state, fall back to state 0
//     when the bare `image` id is not registered.
//
// For phase 3 the three tags share an implementation file because
// their paint paths diverge only on the NStatesButton image suffix.
// Phase 6 will move per-instance state (pressed-state, hover, drag)
// here.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class ButtonWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

class ToggleButtonWidget : public ButtonWidget {};

class NStatesButtonWidget : public ButtonWidget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace WasabiQt
