---
type: Goal
id: goals/any-skin-fidelity
title: The one goal — every skin, past and future, rendered by the original Maki VM
description: >
  qtWasabi's single governing objective, stated with acceptance criteria,
  operating rules, and explicit non-goals. Every design decision, review,
  and roadmap item in the repository is measured against this document.
tags: [goal, fidelity, maki-vm, any-skin, north-star]
timestamp: 2026-07-03T16:30:00+02:00
related:
  - ../index.md
  - ../fidelity/index.md
  - ../roadmap/index.md
---

# The one goal

**qtWasabi has to work for every skin that is already built for the original
Winamp, and for every skin that will be built for the original Winamp in the
future. That is potentially thousands of Maki binaries. Therefore qtWasabi
has to render each of them correctly by executing the original Maki VM.**

This is not one goal among several. It is the reason the project exists.
qtamp, the reference player, matters only as a demonstration that the engine
achieves this.

## Why the Maki VM is non-negotiable

A Winamp Modern skin is not a picture. It is a program: XML declares the
widget tree, and compiled `.maki` bytecode drives its behaviour, drawers
that slide, tabs that switch, tickers that scroll, windows that resize,
regions that reshape, timers that chain. Two skins with identical XML can
behave completely differently because their scripts differ.

No amount of C++ that imitates one skin's scripts can generalize, because
future skins ship scripts nobody has seen yet. The only implementation that
covers the open set of all skins is the one Winamp itself used: run the
skin's own bytecode on the real VM against the real object model, and let
the engine merely supply what the VM calls into (widgets, painting, input,
timers, config).

qtWasabi already vendors and compiles the original VCPU bytecode
interpreter. The gap, documented in [fidelity/maki-vm.md](../fidelity/maki-vm.md),
is the object model and API surface around it.

## Acceptance criteria

The goal is met for a given skin when all of the following hold, with zero
skin-specific branches anywhere in the tree:

1. **Loads clean.** The `.wal`/skin directory parses, all its scripts load
   into the VM, and no script is skipped, faked, or short-circuited.
2. **Scripts run to completion.** Every event a real Wasabi engine would
   dispatch (startup, resize, timers, mouse, keyboard, setXmlParam,
   visibility) reaches the skin's handlers, and every method those handlers
   call is bound with correct semantics and arity.
3. **Pixels match.** Rendering agrees with original Winamp on the same skin
   within the tolerance of font rasterization: geometry exact, colors and
   gammaset tints exact, bitmap slicing exact, alpha behaviour exact.
4. **Behaviour matches.** Drawers, tabs, shade modes, window shaping,
   scrolling, drag, snapping, and animations do what they do in original
   Winamp, driven by the skin's scripts, not by engine-side imitations.
5. **Degrades like the original.** Where a skin depends on a host facility
   (playback state, playlist, media library), the engine's Host interface
   supplies it through the same API the original exposed.

## Operating rules

- **Fix the engine, never the skin.** A wrong-looking skin is always an
  engine defect: a missing binding, a wrong resolver, an unfired event.
- **No new crutches.** Any change that keys on a widget id, GUID literal,
  skin name, or pixel constant taken from one skin is rejected in review.
  Existing ones live in [the crutch register](../fidelity/crutch-register.md)
  until deleted; the register only shrinks.
- **Reference first.** When behaviour is in doubt, the answer is in the
  reference Wasabi sources (`winamp-linux/Src/Wasabi`) or in observable
  original-Winamp behaviour, not in what happens to make one skin look right.
- **Trace, don't guess.** Unknown method calls, unbound classes, and skipped
  events are logged (`WASABIQT_TRACE_UNKNOWN_DLF` and friends); shipped-skin
  traces prioritize binding work by real-world frequency.
- **The corpus is the regression suite.** Offscreen screenshot comparisons
  across a growing set of real skins gate every engine change.

## Non-goals

- Reimplementing skins' behaviour in C++, however faithful it looks.
- Winamp Classic (2.x) skin support; that is a separate, simpler format.
- Binary compatibility with Win32 plugin DLLs; native plugin ports go
  through wasabi-compat instead.

## Measuring progress

Progress is the crutch register shrinking, the bound-method count rising,
and the corpus of skins that pass the acceptance criteria growing. The
[roadmap](../roadmap/index.md) orders the work by how many skins each step
unblocks.
