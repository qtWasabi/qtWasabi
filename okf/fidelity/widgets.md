---
type: Audit
id: fidelity/widgets
title: "Widget fidelity: faithful interpretations vs. substitutes and stubs"
description: >
  Audit of every widget class against real Wasabi behaviour. The bitmap-blit
  family is largely faithful; a second tier is hand-drawn approximation; a
  third tier is placeholder boxes. Lists what each must become.
tags: [widgets, rendering, stubs, audit]
timestamp: 2026-07-03T16:30:00+02:00
related:
  - maki-vm.md
  - crutch-register.md
  - ../roadmap/index.md
---

# Widget fidelity

Paint dispatch is a clean class-per-widget model (TreePainter →
`Widget::paint`). Fidelity is three-tiered.

## Faithful today

Layer, Grid (9-slice), Button (up/down/hover/active precedence), Slider
(3-slice + thumb states), Images (frame strip), ProgressGrid, Status, and
AlbumArt all resolve through Bitmap/Color/Gammaset registries and are
skin-generic. The in-player playlist (PleditHostRenderer over the real
`draw_pe`) and the Media Library direction (MlHostWidget over the real
`wa_dlg` genex) are genuine faithful ports.

## Structurally missing behaviour (high)

- **AnimatedLayer never animates** (`AnimatedLayer.cpp:14`): paints frame 0
  only. Needs the frame timer over `speed/start/end/autoreplay` plus Maki
  `play/stop/gotoFrame`. Freezes every skin with animated chrome.
- **Menu never spawns a popup** (`Menu.cpp:36`): only swaps its down bitmap.
  Needs a real popup group built from the item layers, dismissed on
  click-outside.
- **DropDownList** (`DropDownList.cpp:10`): fixed box + triangle, no items,
  no popup list.
- **TabSheet** (`TabSheet.cpp:12`): fixed 14 px strip, no labels, no
  switching.
- **ScrollBar is fake** (`ScrollBar.cpp:12`): fixed one-third thumb, not a
  bitmap scrollbar bound to content position/pagesize. One real generic
  ScrollBar must serve every list/panel/componentbucket, replacing the
  per-skin scrollbar geometry currently baked into LayerWidget.
- **MediaLibraryPanel** (`MediaLibraryPanel.cpp:314`): a fixed hand-drawn
  copy of Bento's empty library; WindowHolder still routes the library GUID
  here as fallback. Retire in favour of the real gen_ml host path.

## Approximations to make faithful (medium)

- **Edit** (`Edit.cpp:12`): static box, no text entry, caret, or focus. Also
  the blocker for Media Library search-by-typing; requires the engine
  keyboard-input path (see [maki-vm.md](maki-vm.md), event surface).
- **CheckBox** (`CheckBox.cpp:14`): hand-drawn box+tick, ignores skin
  checkbox bitmaps.
- **ComponentBucket** (`ComponentBucket.cpp:8`): no scroll affordance;
  depends on an externally set `_scroll`.
- **EqVis** (`EqVis.cpp:14`): vertical bars instead of the classic
  interpolated EQ curve.
- **Vis** (`Vis.cpp:111`): hardcodes 19 bands; missing peak-hold falloff and
  render styles.
- **Status** (`Status.cpp:17`): guesses transport-bitmap alignment by pixel
  sampling.
- **Splitter** (`Splitter.cpp:12`): fixed non-draggable groove ignoring skin
  divider bitmaps.
- **GuiList / Browser** (`GuiList.cpp:10`): literal placeholder boxes
  labeled "List"/"Web".
- **Popup / PopupMenu / LayoutStatus** (`Popup.cpp:10`): fixed translucent
  boxes.
- **PlaylistPro substitute** (`PlaylistPro.cpp:30`): still present with
  fixed colors; superseded by the pledit host path, delete when nothing
  routes to it.
- **LayerPainter tile-vs-stretch by heuristic** (`LayerPainter.cpp:173`)
  instead of declared scaling semantics.
- **SectionFrame dead `color.ml.frame.*` ids** (`SectionFrame.cpp:30`) with
  a fixed grey bevel fallback.

## The pattern

Substitute widgets exist where the real behaviour needed either the VM event
surface (Menu, TabSheet, Edit, ComponentBucket) or a host data path (GuiList,
Browser, ML). Both dependencies are roadmap workstreams; the widgets then
become thin faithful interpreters instead of pictures.
