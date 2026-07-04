---
type: Audit
id: fidelity/layout-geometry
title: "Layout and geometry: frame constraints, coordinate bugs, window sizing"
description: >
  Audit of the geometry model against reference Wasabi (framewnd.cpp,
  xuiframe.cpp, guiobj.cpp). Two outright coordinate-resolution bugs, an
  incomplete Wasabi:Frame constraint model, and a window-sizing heuristic
  that fights the Maki resize model.
tags: [layout, geometry, frame, resize, audit]
timestamp: 2026-07-03T16:30:00+02:00
related:
  - maki-vm.md
  - crutch-register.md
  - ../roadmap/index.md
---

# Layout and geometry

Reference: `winamp-linux/Src/Wasabi/api/wnd/wndclass/framewnd.cpp` and
`.../skin/widgets/xuiframe.cpp` for frames; `guiobj.cpp` for coordinate
attributes.

## Outright correctness bugs (high)

- **`fitparent="N"` sign inverted** (`widgets/Widget.cpp:113`): reference
  treats negative N as inset and positive as fill; qtWasabi has it backwards
  and mishandles positive N. Wrong geometry on any skin using numeric
  fitparent.
- **`relat*="2"` percentage mode silently treated as absolute**
  (`widgets/Widget.cpp:93`): the tri-state relat model (0 absolute,
  1 parent-relative, 2 percentage) is missing its third state.

## Wasabi:Frame constraint model incomplete (high)

- `maxwidth`/`maxheight` ignored entirely (`Layout.cpp:799`); this is the
  direct cause of the maximized-layout file-info ballooning class of bugs.
- `minwidth`/`minheight` applied as a crude positive per-pane floor rather
  than the reference divider-scalar clamp; negative (parent-minus-N) minima
  unsupported (`widgets/Widget.cpp:148`).
- The SIZERWIDTH=8 gap between panes is dropped; panes abut and the divider
  is overlaid with a Bento-tuned bevel (`Layout.cpp:823`).
- Proportional PULLBAR_HALF/QUARTER splits unsupported; a width-less frame
  stacks both panes (`Layout.cpp:845`).

**Faithful model:** the divider is a single clamped scalar per frame; both
panes derive from it; the sizer gap is reserved and rendered with the
frame's own v/hbitmap; constraints clamp the scalar, not the panes.

## Window sizing fights the VM (high)

`SkinQuickItem.cpp:373` auto-shrinks the toplevel to the painted-alpha
extent. Real Wasabi never does this: layout size is the layout's `w/h` as
mutated by scripts (`setTargetH`/`gotoTarget`/`resize`). The heuristic
produced the drawer-close window-collapse bug class; the current
`minimum_h` floor is a mitigation. The faithful end state drives window
size purely from the layout root + the Maki onResize fixpoint and deletes
the heuristic.

## Crutches in the geometry path

Registered in [crutch-register.md](crutch-register.md): the `_shift_y`
standardframe stamping, the static `titlebar.m` reimplementation
(`Layout.cpp:1346`), the id-substring menubar chrome (`Layout.cpp:422`),
`stripNarrowingColumns` (`Layout.cpp:1709`), and the negative-w/h
auto-relat promotion (`widgets/Widget.cpp:128`, a typo-forgiveness crutch;
alias the known `relaw` misspelling instead).

## Omitted reference features (medium)

`renderRatio` (global UI scale), `autoSysMetrics` font-scale, and AUTOWH
auto-sizing are absent from the shared resolver (`widgets/Widget.cpp:86`).
Skins authored against them mis-size on qtWasabi.
