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
    // Buttons claim clicks even without an `id` attr — they're
    // inherently interactive (their `action=` fires regardless of
    // whether a Maki script has bound a handler).
    bool isInteractive() const override { return true; }

    // Canonical Wasabi state machine — down > hover > active > normal
    // (buttwnd.cpp:353-356).  ButtonWidget tracks the first two;
    // ToggleButtonWidget adds m_activated.  Events come from
    // SkinQuickItem's hover handlers + qtamp's mousePressEvent.
    void onLeftButtonDown(QPoint, PaintCtx &) override;
    void onLeftButtonUp  (QPoint, PaintCtx &) override;
    void onMouseMove     (QPoint, PaintCtx &) override;
    void onMouseLeave    (PaintCtx &)         override;

protected:
    bool m_pressed = false;
    bool m_hover   = false;
    // Pick the attr name (`downImage` / `hoverImage` / `activeImage` /
    // `image`) the current state precedence selects, falling back to
    // the next-lower priority when the selected slot is missing.
    // ToggleButtonWidget overrides to consider m_activated.
    virtual QString currentImageAttr() const;
};

class ToggleButtonWidget : public ButtonWidget {
public:
    void setXmlParam(const QString &name, const QString &value) override;
protected:
    QString currentImageAttr() const override;
    bool m_activated = false;
};

class NStatesButtonWidget : public ButtonWidget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
};

}  // namespace WasabiQt
