---
type: Roadmap
id: roadmap/index
title: "Convergence roadmap: from crutches to the Maki VM"
description: >
  The leverage-ordered plan that closes the audited gap between qtWasabi
  today and the one goal. Workstreams are named, not numbered; order within
  the list is priority. Each names the crutch-register entries it deletes
  and its exit criterion.
tags: [roadmap, convergence, workstreams]
timestamp: 2026-07-03T16:30:00+02:00
related:
  - ../goals/any-skin-fidelity.md
  - ../fidelity/index.md
  - ../fidelity/crutch-register.md
---

# Convergence roadmap

Ordering principle: fix what the most scripts trip over first. The VM
object model is upstream of everything; the paint-fidelity work is
independent and can run in parallel with any of it.

## Object model and dispatch (the keystone)

Restore the real Maki class layer around the vendored interpreter: a live
`ObjectTable` (`getClassFromName`/`getClassFromGuid`), typed
`instantiate()` for `Timer/Region/Map/List/...`, GUID-keyed
`getInterface`, and method lookup scoped by (class, method) instead of the
flat name table. Derive CALLM arity from the DLF/bytecode so unseen methods
cannot desync the operand stack.

- Deletes/unblocks: the `stop`/`getPosition`/`getWidth` collision
  workarounds, attr-sniffing dispatch, the guru cascade on downcasts.
- Exit: a skin can `new` and downcast every scriptable class; no method
  resolves to the wrong receiver anywhere in the corpus.

## Event surface

Fire the full engine-to-script event set from widget input and lifecycle:
mouse (`onLeftButtonDown/Up`, `onMouseMove`, enter/leave, wheel), keyboard
(`onChar`, `onKeyDown/Up`, focus), Slider `onSetPosition`, Button
`onActivate/onToggle`, `onStartup` and container/layout lifecycle at load.
Keyboard routing to holders is part of this (it also unblocks Media Library
search-by-typing and the Edit widget).

- Deletes: C++ per-widget interactivity stand-ins, the Slider host-binding
  bypass, hardcoded button verbs.
- Exit: a script-driven skin is fully interactive with the engine's own
  interactivity code acting only as the script-less fallback.

## One state store and a real Config service

Collapse `CfgAttribStore` and the VM's `privateIntStore` into one persisted
store shared by bindings and widgets; implement `Config.newItem`/
`getAttribute`/`onValueChanged` on it. Remove the pre-seeded
`winamp5|DrawerOpen/ConfigTab` keys and the `kScriptHiddenByDefault` list:
initial visibility comes from the XML as authored plus whatever the skin's
`onScriptLoaded` sets.

- Deletes: both startup-state fakes, the store duality, widget-owned
  toggle/tab/stepper logic.
- Exit: the config drawer and tabs of any skin open, switch, and persist
  purely through that skin's scripts. The embedder's `setDrawerOpen`/
  `switchDrawerTab` are deleted the same day.

## Containers and layouts

Real per-container Layout objects: `getContainer(name).getLayout(id)`
resolves to the correct object; `hide()/show()` land on the intended
window; shade mode switches to the skin's real `<layout id="shade">`.
Window size derives from the layout root's `w/h` as mutated by scripts
(`setTargetH`/`gotoTarget`/`resize`) through the onResize fixpoint; the
painted-alpha auto-shrink heuristic is then deleted.

- Deletes: refuse-to-hide-active-root, `toggleShade` strip, auto-shrink
  (and its minimum_h mitigation), the 750 ms resize block, compactH.
- Exit: multi-layout and multi-window skins navigate correctly; drawer
  open/close never mis-sizes a window on any corpus skin.

## Geometry correctness

The reference Wasabi:Frame model (divider as one clamped scalar, maxwidth/
maxheight honored, negative minima, SIZERWIDTH=8 reserved and rendered from
the frame's own bitmaps, proportional pullbar splits), plus the
`fitparent` sign fix, `relat*="2"` percentage mode, `renderRatio`,
and AUTOWH auto-sizing.

- Deletes: the Bento bevel=7 divider overlay, the negative-w/h auto-relat
  promotion, the maximized-layout ballooning bug class.
- Exit: geometry matches original Winamp pixel-for-pixel on the corpus at
  native and resized window sizes.

## Bindings breadth

Bind the no-op tail by measured need: Region and Map (window shaping,
color maps), AnimatedLayer control, List/Tree, `Wac.sendCommand`, System
transport, alpha/fade methods, `setTargetSpeed`/`setTargetA`. Priorities
come from `WASABIQT_TRACE_UNKNOWN_DLF` telemetry across the corpus. As the
titlebar/standardframe/stepper scripts start running whole, delete
`runKnownScripts`, `applyTitlebarResize`, `wireSteppers`, `_shift_y`
stamping, and `stripNarrowingColumns` (region contribution becomes a pure
function of sysregion layer alpha).

- Deletes: every static known-script mirror, the region heuristics, the
  scripted-fade and animation-speed stubs.
- Exit: tracing a corpus run shows no unknown-method hits on any skin.

## Widget faithfulization

One generic bitmap ScrollBar bound to content everywhere; Menu spawns a
real popup; DropDownList, TabSheet, Edit (caret/focus/entry), CheckBox from
skin bitmaps, ComponentBucket scrolling, EqVis interpolated curve, Splitter
dragging, GuiList as a real list. Finish the Media Library on the pledit
pattern (host real `gen_ml` output themed by `wa_dlg`), then delete
`MediaLibraryPanel` and the `PlaylistPro` substitute.

- Deletes: every placeholder box, the hand-drawn ML, the LayerWidget
  volumebar/scrollbar constants.
- Exit: no widget class paints a fixed approximation; every affordance a
  skin declares is live.

## Paint fidelity

`pixelSize = round(fontsize * 3/4)` with default 14 and honored paddings;
bitmap-font advance per `do_textOut` with `timecolonwidth`/`forcefixed` as
the Text time-display mode; tint only explicit gammagroups (synthetic
recolor stays opt-in for theme-less skins); default "Text" colorgroup and
`filterColor` for color tokens; magenta-colorkey for classic BMP assets.

- Deletes: the colon heuristics, the gammagroup guessers, the +2 inset.
- Exit: text and tint match original Winamp on the corpus within
  rasterizer tolerance.

## Corpus verification (continuous, not a stage)

Grow the offscreen screenshot harness into a corpus gate: many real skins
rendered and diffed per change, unknown-DLF telemetry aggregated, the
acceptance criteria from [the goal](../goals/any-skin-fidelity.md) checked
per skin. Every workstream above lands behind this gate. The first corpus
is the five QTAMP showcase forks diffed against their authors' reference
screenshots; as of 2026-07-04 only WinampModernPP passes, and the four
broken ones are tracked per-skin in
[fidelity/corpus-status.md](../fidelity/corpus-status.md).

- Exit: never; this is the standing regression discipline that keeps the
  register at zero once it gets there.
