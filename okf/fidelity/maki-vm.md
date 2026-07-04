---
type: Audit
id: fidelity/maki-vm
title: "Maki VM completeness: the root gap"
description: >
  Audit of the Maki VM layer against the goal. The bytecode interpreter is
  the original; the class/dispatch/object model around it is a name-based
  shim. This one gap is the reason nearly every crutch in the register
  exists.
tags: [maki, vm, dispatch, bindings, audit]
timestamp: 2026-07-03T16:30:00+02:00
related:
  - ../goals/any-skin-fidelity.md
  - crutch-register.md
  - ../roadmap/index.md
---

# Maki VM completeness

> STATUS 2026-07-04: the transport slice of the event surface is
> closed: the engine derives System.onPlay/onResume/onPause/onStop
> from the same host status getStatus() reads (one source, no embedder
> wiring).
>
> STATUS 2026-07-05, keystone phase A LANDED: the class registry is
> real. 54 classes with their interop GUIDs and ancestor links resolve
> the .maki GUID tables (getClassFromGuid/getClassFromName), every
> script owns its type list (the shared static also corrupted
> OPCODE_NEW/UMV across loads), addrefDLF resolves (declared class,
> method) via the ancestor walk, and new-constructed objects carry
> their class. Migration is evidence-first: scoped rows inherit body
> and arity from the flat table, scoped misses fall back flat and are
> traced (WASABIQT_TRACE_SCOPED_MISS — 308 pairs on Bento form the
> table-population worklist). First splits landed: Slider and Frame
> get/setPosition no longer attr-sniff. Remaining phases: typed
> instantiate depth (Timer/Region/Map behaviours keyed off the class
> stamp), GUID-keyed getInterface + real typeCheck, then delete the
> flat fallback.
>
> STATUS 2026-07-05, phase A2 LANDED: all 36 reference exported-method
> tables are transcribed into the registry (SystemObject 220,
> GuiObject 103, GuiList 89, ... plus the Object root).  The scoped
> fallback dropped from 396 (class, method) pairs to ONE across the
> eight-skin corpus (SystemObject.onCurrentTrackRated, a WACUP-era
> event the 5.666 surface genuinely lacks); still-unbound methods now
> fail soft at their reference arity, closing the arity-guess
> stack-desync class.
>
> STATUS 2026-07-05, phase B LANDED: typed instances.  `new`
> List/BitList/Map/Region carry real behaviour keyed off the class
> stamp (Map via BitmapRegistry with the reference channel semantics,
> Region QRegion-backed incl. loadFromMap threshold+inverted, List
> with faithful index/string-compare semantics); ObjectTable::destroy
> performs class-aware teardown (a deleted Timer kills its QTimer —
> the zombie fix) while the ScriptObject stays alive;
> vcpu_getInterfaceObject honours GUIDs for stamped instances via the
> registry ancestor walk.  Remaining: real typeCheck + per-class
> singletons (needs the Config service, #237), then delete the flat
> fallback and the alias rows.

The opensourced VCPU bytecode loop (`vcpu.cpp`, patched, vendored) executes
compiled `.maki` verbatim. Everything around it that real Wasabi provides,
the typed GuiObject class hierarchy, per-class method dispatch, the event
surface, the Container/Layout model, and the preference store, is currently
replaced by a class-less, name-based shim. That shim is what forces the
engine and embedder to imitate skin scripts in C++.

## Blockers

### No class model
`wasabi-port/wasabi-port-link-stubs.cpp:1354`: `ObjectTable::
getClassFromName/getClassFromGuid` return `-1`; `instantiate()` returns one
generic `WidgetScriptObject` for every `new Timer/Region/Map/List`;
`vcpu_getInterfaceObject` returns `this` for any GUID. There are no typed
instances, so scripts cannot construct or downcast objects the way every
nontrivial skin does.

**Faithful:** restore `ObjectTable` with real class registration, typed
`instantiate`, and GUID-keyed `getInterface`.

### Flat name-based method dispatch
In `wasabi-port/wasabi-port-link-stubs.cpp:1310`, `addrefDLF` resolves a
method by NAME against one global table. Cross-class collisions misroute:
`stop` lands on Timer.stop even when called on System; `getWidth/getValue`
collide between GuiObject and Map; `getPosition` is disambiguated by
attribute-sniffing (`maki-bindings.cpp:1596`).

**Faithful:** scope lookup by `(class, method)` once the class model exists.

### Event surface is a small subset
`public/qtWasabi/Widget.h:173`: widgets receive C++ input but do not fire
the matching Maki handlers (`onLeftButtonDown/Up`, `onMouseMove`,
`onEnterArea/onLeaveArea`, `onMouseWheel`, `onChar/onKeyDown/onKeyUp`,
Slider `onSetPosition`, Button `onActivate/onToggle`, Layout/Container
lifecycle, `onStartup`). Skins whose interactivity lives in scripts appear
dead, so C++ stands in for them.

**Faithful:** every widget input handler fires the corresponding handler on
its bound script object; engine-side interactivity becomes the fallback for
script-less skins only.

### ~710 of ~869 methods are no-ops
In `wasabi-port/maki-bindings.cpp:1705`, arity-only stubs returning 0 back
whole classes: Region, Map, AnimatedLayer, List/Tree/Menu, `Wac.sendCommand`,
System transport (`play/pause/next/previous/eject/seekTo`). Skins calling
them silently do nothing.

**Faithful:** bind by real-skin call frequency using
`WASABIQT_TRACE_UNKNOWN_DLF` traces from the corpus.

## High

- **Unknown-method arity defaults to 0** (`wasabi-port-link-stubs.cpp:1293`):
  a legacy CALLM with `np=-1` on an unseen method desyncs the operand stack
  and cascades guru meditations. Arity must derive from the DLF/bytecode,
  not from the hand-maintained 869-name list.
- **`getInterface()` returns null** → the next method call on the cast
  result gurus. Blocks every skin using explicit downcasts.
- **Config is a shared dummy** (`maki-bindings.cpp:1408`): no persisted
  store, `cfg_*` unbound. Preference-gated skin behaviour cannot branch.
- **Container/Layout collapse to a single layout-root pseudo**
  (`maki-bindings.cpp:1646`): `getContainer(name).getLayout(id)` cannot
  navigate, so multi-window and multi-layout scripts fail; hiding the active
  layout root is refused outright (`SkinRuntimeBridge.cpp:700`).

## Medium

- Button action verbs handled in C++ (`SkinRuntimeBridge.cpp:764`) and the
  Slider host binding bypassing `onSetPosition` (`:783`) instead of the
  script/action model.
- `setAlpha/getAlpha` constant stubs (`maki-bindings.cpp:1173`): scripted
  fades are dead.
- Target animation is a fixed 350 ms tween; `setTargetSpeed` ignored,
  `setTargetA` unbound (`maki-bindings.cpp:716`).
- `onStartup` / System lifecycle events not dispatched at load
  (`SkinRuntime.cpp:442`).

## Why this file is first

Every subsystem audit traced its crutches back here. The drawer hardcodes
exist because `configtabs.m` cannot fully run; the titlebar reimplementation
exists because `titlebar.m`'s resize chain cannot fully run; force-visible
lists existed because Timer chains did not complete. Close this layer and
the crutch register drains; polish widgets first and the register grows.
