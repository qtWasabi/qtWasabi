---
type: OKF Bundle
id: index
title: qtWasabi — Any-Skin Fidelity Bundle
description: >
  Open-Knowledge-Format bundle for qtWasabi, the open-source engine that
  renders Winamp Modern (.wal / Wasabi) skins by executing their own compiled
  Maki bytecode. Records the project's single governing goal (render every
  skin ever built, or yet to be built, for original Winamp), the audited gap
  between that goal and the current engine, and the convergence roadmap.
tags: [qtwasabi, wasabi, maki, winamp, modern-skins, okf, fidelity]
timestamp: 2026-07-03T16:30:00+02:00
related:
  - goals/any-skin-fidelity.md
  - fidelity/index.md
  - roadmap/index.md
---

# qtWasabi — Any-Skin Fidelity Bundle

This bundle documents qtWasabi in the Open Knowledge Format: Markdown files
with YAML front matter, file path as identity, cross-references as relative
links. It is the single place a contributor can read to understand what
qtWasabi must become, precisely how far it is from that today, and in which
order the remaining distance closes.

## The one goal

> **qtWasabi has to work for every skin that was ever built for the original
> Winamp, and for every skin that will be built for it in the future.
> Potentially thousands of Maki binaries. Therefore qtWasabi has to render
> them correctly by executing the original Maki VM.**

Everything in this repository is subordinate to that sentence. It is spelled
out, with acceptance criteria and non-goals, in
[goals/any-skin-fidelity.md](goals/any-skin-fidelity.md).

## What follows from the goal

1. **The Maki VM is the spec.** A skin's behaviour is defined by its own
   compiled scripts running against the real Wasabi object model. Whenever
   qtWasabi substitutes C++ logic for what a skin's script would do, that is
   a defect, even if it happens to look right on one skin.
2. **There is no per-skin code.** No `if (id == "player.normal.drawer")`,
   no literal skin names, no pixel constants copied out of one skin's XML.
   Each such crutch is registered in
   [fidelity/crutch-register.md](fidelity/crutch-register.md) with the
   generic mechanism that must replace it.
3. **Thousands of shipped skins are the test corpus.** A fix is engine-level
   and general, or it is not a fix.

## State of the engine (audited 2026-07-03)

A six-dimension audit of the whole tree (engine, embedder, and the reference
Wasabi sources) produced roughly 80 structured findings. Summary and the
per-dimension detail live under [fidelity/](fidelity/index.md):

- [Maki VM completeness](fidelity/maki-vm.md) — the root gap. The bytecode
  interpreter is the original, but the class/dispatch layer around it is a
  name-based shim: no typed object model, one flat method table, most of the
  Wasabi API surface stubbed.
- [Crutch register](fidelity/crutch-register.md) — every skin-specific
  hardcode in engine and embedder, each mapped to its generic replacement.
- [Widget fidelity](fidelity/widgets.md) — which widgets are faithful
  interpretations and which are hand-drawn substitutes or placeholders.
- [Layout and geometry](fidelity/layout-geometry.md) — Wasabi:Frame
  constraints, coordinate-resolution bugs, and the auto-shrink heuristic.
- [Text, color, bitmaps](fidelity/text-color-bitmap.md) — font metrics,
  gammaset/tint fidelity, bitmap semantics.

## The road

The leverage-ordered convergence plan is
[roadmap/index.md](roadmap/index.md). Its center of gravity: restore the
real Maki class model and event surface first, because nearly every crutch
in the register exists only to paper over that one gap.
