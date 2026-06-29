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
//   • NStatesButton — multi-state (e.g. Repeat: off/all/one).  Per
//     the Wasabi convention it cycles through `nstates` and suffixes
//     `image` with the current state index — `image="player.songinfo.repeat"`
//     → actual bitmap ids `repeat0`, `repeat1`, `repeat2`.  When the
//     state isn't being driven, fall back to state 0 when the bare
//     `image` id is not registered.
//
// The three tags share an implementation file because their paint
// paths diverge only on the NStatesButton image suffix.  Per-instance
// state (pressed-state, hover, drag) lives here too.
//

#include <qtWasabi/Widget.h>

namespace qtWasabi {

class ButtonWidget : public Widget {
public:
    ~ButtonWidget() override;
    void onAttrsInitialized() override;
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
    // Buttons claim clicks even without an `id` attr — they're
    // inherently interactive (their `action=` fires regardless of
    // whether a Maki script has bound a handler).
    bool isInteractive() const override { return true; }

    // State-image precedence — down > hover > active > normal.
    // In the Wasabi convention a plain <button> ignores activeImage;
    // only ToggleButton/NStates use it.
    // We diverge there for one practical reason: Modern skins (Bento /
    // WACUP / WinampModernPP) ship `<button activeImage=...>` on plain
    // <button>s and rely on Maki scripts calling `setActivated` to
    // light them up (the EQ on/off + LED pair is the canonical case).
    // Without a separate engine state service, when the widget carries
    // both `activeImage` AND a recognised toggle-shape action (or any
    // cfgattrib), we self-toggle on click and use the action (or
    // cfgattrib) as the sync key so the LED sibling stays in sync
    // without per-skin special-casing.
    void onLeftButtonDown(QPoint, PaintCtx &) override;
    void onLeftButtonUp  (QPoint, PaintCtx &) override;
    void onMouseMove     (QPoint, PaintCtx &) override;
    void onMouseLeave    (PaintCtx &)         override;

    // Maki / cfgattrib hook — also handles `activated="0"/"1"` so
    // sibling buttons sharing the same sync key stay in sync.
    void setXmlParam(const QString &name, const QString &value) override;

protected:
    bool m_pressed   = false;
    bool m_hover     = false;
    bool m_activated = false;
    // autotoggle=0 means the button does NOT flip its own state on click
    // (a script / cfgattrib drives it instead); default is self-toggling.
    bool m_autoToggle = true;
    // cfgval= is the integer written to the bound cfgattrib when ON
    // (OFF always writes 0); default 1.
    int  m_cfgVal    = 1;
    QString m_cfgKey;
    int     m_cfgSubHandle = 0;

    // Pick the attr name (`downImage` / `hoverImage` / `activeImage` /
    // `image`) the current state precedence selects, falling back to
    // the next-lower priority when the selected slot is missing.
    // NStatesButton overrides to derive activated from `m_state != 0`.
    virtual QString currentImageAttr() const;

    // True when this button manages an activated state (has activeImage
    // and either a cfgattrib OR an action that looks like a toggle).
    // Drives self-toggle on click + cfgattrib sync subscription.
    bool isToggleShaped() const;

    // Derive the sync key for cfgattrib-style coupling.  Prefers an
    // explicit `cfgattrib="…"`; otherwise falls back to a pseudo-key
    // built from the `action=` attr so plain <button>s with the same
    // action share state (EQ_TOGGLE → `__action:EQ_TOGGLE`).
    QString resolvedCfgKey() const;

    // Wire up the cfgattrib subscription if this widget has a sync
    // key.  Called from onAttrsInitialized in both ButtonWidget and
    // (via super) from the NStates / Toggle overrides.
    void wireCfgAttrib(int initialValue);

    // Hook fired by onLeftButtonUp once the press has cleared and the
    // widget had previously claimed the press.  Default toggles
    // activated state via setXmlParam (so cfgattrib sync fires).
    // NStatesButton overrides to advance state instead.
    virtual void cycleOnRelease();

    // Stepper pattern — canonical Wasabi structure used by the
    // crossfade controls (and any similar +/- bound to a slider
    // cfgattrib).  Activated when the button's id ends in
    // `Decrease` or `Increase` (case-insensitive) AND a sibling
    // widget in the same parent declares a `cfgattrib=` with `high=`
    // (defines the int range, optional `low=` defaults to 0,
    // optional `step=` defaults to 1).  On release the button reads
    // the current value from CfgAttribStore and writes
    // clamp(current ± step, low, high).  Engine-level — no per-skin
    // XML edits needed.  m_stepperDelta is +1 or -1 depending on
    // which suffix matched; zero means "not a stepper".
    QString m_stepperKey;
    int     m_stepperLow   = 0;
    int     m_stepperHigh  = 0;
    int     m_stepperStep  = 1;
    int     m_stepperDelta = 0;

    // Tab-button pattern — set by Layout::wireTabs when this button
    // belongs to the canonical Wasabi `switch.X` / `wdh.X` tab
    // family.  On release we write `m_tabValue` to `m_tabKey` in
    // CfgAttribStore.  A separate subscription keeps `m_activated`
    // in sync so the active-tab LED tracks even though the cfgattrib
    // value semantics here are integer-equals (not just non-zero).
    QString m_tabKey;
    int     m_tabValue   = -1;
    int     m_tabSubHandle = 0;
};

class ToggleButtonWidget : public ButtonWidget {
public:
    // Behaviour intentionally identical to ButtonWidget with
    // `activeImage` — in the Wasabi convention <togglebutton> has the
    // same widget semantics, just spelled differently in the XML.
    // Subclass kept for factory dispatch and for ToggleButton-only
    // features (cfgval interpretation, in particular).
};

class NStatesButtonWidget : public ButtonWidget {
public:
    void onAttrsInitialized() override;
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
protected:
    // Current state index, 0..(nstates-1).  Click advances by one
    // and wraps.  With autoelements=1 (default) the engine appends
    // the state suffix to image/down/hover bitmaps; with =0 the
    // visual derives from `m_state != 0` driving activeImage instead.
    int m_state = 0;
    // `cfgvals` translates state index ↔ stored value (e.g. cfgvals
    // "0;1;-1" means state 2 stores -1).  Missing cfgvals means
    // state index IS the stored value.
    QStringList m_cfgVals;

    void cycleOnRelease() override;

    int stateToValue(int state) const;
    int valueToState(int value) const;
};

}  // namespace qtWasabi
