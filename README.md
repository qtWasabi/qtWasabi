# WasabiQT

A Qt6 port of Winamp's **Wasabi** skin engine, scoped as a self-contained
library you can drop into any Qt application that wants to host classic
Winamp Modern (`.wal`) skins on Linux, macOS, or Windows — no DirectX,
no Win32-specific service registry, no networking stack you don't need.

Pronounced "wasabi-cute". Pun intended.

## Why this exists

Modern Winamp skins are scripted in a bytecode language called **Maki**,
running on a stack VM specified in `Src/Wasabi/api/script/vcpu.cpp` of
the opensourced Winamp source. Thousands of shipped skins exercise quirks of
that VM and the surrounding widget classes — every text widget's `+2`
left inset, every `getAutoWidth + 4 + lpadding` measurement, every
`<sendparams>` scope rule, every `clientToScreenX/screenToClientX`
behaviour. Re-implementing the VM from scratch is a years-long
bug-hunt because the spec lives in the running code, not the docs.

The original Winamp team eventually arrived at the same conclusion:
their planned **Wasabi 2** rewrite kept the VM and widget classes
unchanged and reorganised the *service plumbing* around them. They
also started a Qt6 adapter (`Src/Wasabi/qt6/QtWindowAdapter`,
`QtCanvasAdapter`) before the project went dormant.

WasabiQT picks up that thread:

- **Keep** `Src/Wasabi/api/script/` — the original Maki VM, exactly as
  shipped. All bytecode quirks, all bindings, all 25 opcodes.
- **Keep** `Src/Wasabi/api/skin/widgets/` — the original Group, Layer,
  TextFrame, Button, Slider, Animation, Timer, Container etc. classes.
  Their layout/positioning logic IS the spec for skin compatibility.
- **Keep** `Src/Wasabi/api/skin/skinparser.cpp` — the original XML loader.
- **Replace** `Src/Wasabi/api/wnd/platform/win32/` and `…/osx/` — the
  OS window/canvas backend. WasabiQT renders through `QPainter` and
  routes events through `QWidget`.
- **Stub** parts of the Wasabi service registry the widget tree doesn't
  need (jnetlib, gracenote, AVS host integration, ML SQL, etc.) so the
  build stays focused.
- **Vendor** BFC (Wasabi's COM-like foundation framework) as-is. It's
  ~4500 lines of portable-ish C++; just needs a Win32 type shim.

The result: a static or shared library that exposes a small C++ host
interface, pulls in everything Wasabi needs to load a skin, and renders
through Qt. No external runtime dependencies beyond Qt6.

## Status

**Bootstrapping.** This repo currently has only the README. The plan is:

1. Vendor `Src/Wasabi/bfc/` and the Win32 type shim (`win32_types.h`)
   so BFC compiles on Linux clang/gcc.
2. Vendor `Src/Wasabi/api/script/` and get the VM compiling against BFC.
3. Vendor `Src/Wasabi/api/wnd/` and adapt `BaseWnd::clientToScreen`
   etc. to route through `QtCanvasAdapter`.
4. Vendor `Src/Wasabi/api/skin/widgets/` + `Src/Wasabi/api/skin/parsers/`
   and load a minimal skin (Winamp Modern's titlebar group).
5. Wire mouse/paint/keyboard events through `QtWindowAdapter`.
6. First end-to-end milestone: render WACUP/Winamp Modern's player
   titlebar correctly — text centred, streaks padded around the
   sysmenu icon and min/restore/close trio. (This is the same target
   we hit during the libwasabiq prototype, so the test harness from
   that effort transfers directly.)

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│  Embedder (winamp-linux, Audacious plugin, anything Qt6)         │
│   ├─ implements WasabiQt::Host (~40 virtual methods)             │
│   └─ creates a WasabiQt::Skin, embeds it in a QWidget            │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│  WasabiQT (this repo)                                            │
│                                                                  │
│   public/   ← thin C++ façade for embedders                      │
│   src/      ← integration glue (Host adapter, Skin loader)       │
│   qt6/      ← QtWindowAdapter, QtCanvasAdapter (rendering only)  │
│   wasabi/   ← vendored Src/Wasabi/ subset (UNMODIFIED VM)        │
│   bfc/      ← vendored Src/Wasabi/bfc/ + win32_types.h shim      │
│   tests/    ← skin loader + reference-image regression harness   │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
                          Qt6 (Core, Gui, Widgets)
```

## Why not just "Qt-ify" the opensourced source in place?

The opensourced source is a build-system maze — Visual Studio, Xcode,
Android.mk, three platform backends, dozens of optional services that
pull in jnetlib, gracenote, MS Edge components, codec stacks, ML SQL,
and an AVS visualizer engine. Each is a build dependency you don't need
to render a skin.

WasabiQT vendors only the *widget tree + Maki VM + necessary BFC* —
the irreducible core a skin actually exercises — and provides one
CMake target. Embedders compile a single library against Qt6 and they
have a Wasabi skin engine.

## License

Wasabi was historically permissive; the 2024 Llama Group source release
ships under the **Winamp Collaborative License** which permits private
use and modification but not redistribution of modified versions. This
repo is therefore **private to the Winamp-fork ecosystem**; it is not
intended as a public open-source library.

## Inspirations

- `Src/Wasabi/qt6/` — the never-completed Wasabi 2 Qt adapter that
  prompted this design. Same goal, less of the rest of Wasabi 2's
  service-rewrite scope.
- `~/git/winamp-linux/libwasabiq` (deleted, see git history) —
  earlier clean-room re-implementation that taught us where every
  visual gap lives in `Src/Wasabi/`. Test harness and host interface
  design carry over.
