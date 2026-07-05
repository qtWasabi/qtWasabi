---
type: Audit Index
id: fidelity/index
title: "Fidelity audit: where qtWasabi stands against the one goal"
description: >
  Landing page for the 2026-07-03 six-dimension audit of qtWasabi and its
  reference embedder against the any-skin goal. Names what is already
  faithful, the root gap, and links the per-dimension findings.
tags: [audit, fidelity, index]
timestamp: 2026-07-03T16:30:00+02:00
related:
  - ../goals/any-skin-fidelity.md
  - maki-vm.md
  - crutch-register.md
  - ../roadmap/index.md
---

# Fidelity audit (2026-07-03)

Six parallel audits swept the engine (`src/`, `wasabi-port/`,
`wasabi-compat/`), the reference embedder (`qtamp/src/main.cpp`), and the
reference Wasabi sources, producing roughly 80 structured findings.

## What is already faithful (the spine)

These are the convergence targets, already real, that the rest of the
engine must be pulled onto:

- One Maki VM per window, running the skins' own vendored bytecode
  (`vcpu.cpp` verbatim).
- A per-object `onResize` cascade iterated to a geometry fixpoint
  (`SkinRuntime.cpp`).
- `onLeftClick`/`onAction` dispatch that bubbles up the parent chain
  (`SkinQuickItem.cpp`).
- `gotoTarget` tweens that fire `onTargetReached`; Maki Timers backed by
  real QTimers (`SkinRuntimeBridge.cpp`).
- WindowHolder-GUID hosting of the real `draw_pe` for the playlist editor
  (`pledit/`), the pattern the Media Library is being moved onto, and the
  `wa_dlg`/genex GDI raster core that themes it.
- Byte-exact gammaset tint math; `<include>`-inlined color elements.

Older crutches already retired the right way: `kForceVisibleByDefault`,
wireTabs tab-tagging, `applyPlaylistEnlarge`.

## The root gap

**The VM has no object model.** Class lookup returns -1, `instantiate()`
hands back one generic dummy, method dispatch is name-based against a
single flat table, roughly 710 of 869 API methods are no-ops, and most
input/lifecycle events never reach scripts. Every drawer, tab, titlebar,
and visibility crutch in the [register](crutch-register.md) exists because
some script could not run to completion, so C++ imitated its effect for one
skin. Detail: [maki-vm.md](maki-vm.md).

## The ring of shadow machinery

Around the faithful spine sits parallel C++ that shadows the VM instead of
deferring to it: hardcoded startup visibility lists, a config store
pre-seeded with one skin's keys, a dummy Config service, a second attribute
store driving widget state in parallel with the VM's, static known-script
mirrors (titlebar, steppers) still running as the primary pass for
subwindows, and hand-composed substitute renderers. Each encodes Winamp
Modern's ids, keys, or naming conventions, so a differently-authored skin
gets wrong initial visibility, dead tabs, or a mis-themed window.

## Per-dimension findings

- [Maki VM completeness](maki-vm.md): class model, dispatch, events,
  bindings breadth, config, containers.
- [Crutch register](crutch-register.md): every per-skin hardcode with its
  generic replacement; the deletion checklist.
- [Widget fidelity](widgets.md): faithful vs. substitute vs. placeholder,
  per widget class.
- [Layout and geometry](layout-geometry.md): coordinate-resolution bugs,
  the Wasabi:Frame constraint model, window sizing.
- [Window regions](window-regions.md): sysregion z-order composition,
  the visible silhouette vs the input region, the drawer-bar/cut-corner
  artifact pair and its fix history.
- [Text, color, bitmaps](text-color-bitmap.md): font metrics, tint
  fidelity, bitmap semantics.
- [Corpus status](corpus-status.md): per-skin fidelity against the skin
  authors' own reference screenshots (the showcase forks are the first
  regression corpus; 1 of 5 currently faithful).

## How this feeds the roadmap

The [roadmap](../roadmap/index.md) orders workstreams by how many register
entries each one deletes. The VM object model comes first because it is
upstream of nearly everything else.
