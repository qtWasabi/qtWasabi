# WasabiQT

A Qt-native skin engine **inspired by** Winamp's Wasabi 1 + Wasabi 2,
built as its own project — not a port, not a fork. API-compatible with
classic Winamp Modern (`.wal`) skins, but written ground-up against
modern Qt and modern C++.

## What it is

WasabiQT loads Wasabi-format skins (XML widget trees + `.maki`
bytecode), parses them the way Wasabi does, runs scripts the way the
original Maki VM does, and renders everything through `QPainter` /
`QWidget`. The semantics — coordinate conventions, sendparams scopes,
text-widget metrics, group/instance resolution, alignment quirks — all
match the documented behaviour of the opensourced Wasabi 1 and Wasabi 2
sources, because that's the only spec for what shipped skins expect.

But the codebase is its own project. The opensourced source is **reference
material**, not a build target.

## Why this matters

The dream is selfish: I want to finally listen to music on my Apple
M-Series Mac the way I did on Windows in the early 2000s — Winamp
running natively on **Asahi Linux** and **macOS**. No Wine, no x86
emulation, no nostalgia-VM in a window. Just the actual skin engine,
on aarch64 silicon, painted by the same Qt that already gives me a
beautiful desktop on Asahi.

The bigger goal: a skin engine that any themeable Winamp clone can
embed. **Audacious**, **WACUP**, the next thing, and the thing after
that. Wasabi was a great UI framework that nobody else could use because
it shipped welded to one player's runtime. WasabiQT is the small,
embeddable piece you actually wanted.

## Inspirations, not derivatives

- **Wasabi 1** (`Src/Wasabi/api/`) — the original widget classes
  (`Group`, `Layer`, `TextFrame`, `Button`, `Slider`, `Animation`,
  `Timer`, `Container`) and the Maki VM (`Src/Wasabi/api/script/`).
  Their behaviour is the spec; we match it. Their *code* is reference.

- **Wasabi 2** (`Src/replicant/`) — the never-completed cross-platform
  rewrite. It reorganised Wasabi's service plumbing without touching
  the VM or widget contracts. WasabiQT is in that spirit: keep skin
  compatibility, modernise everything around it.

- **Wasabi 2's `Src/Wasabi/qt6/` adapter** (~2015) — the abandoned
  `QtWindowAdapter` / `QtCanvasAdapter` shim that bridged Win32 HWND/HDC
  concepts to QWidget/QPainter. Useful as a **reference** for what to
  expose, but the implementation targets a 2015-era Qt. WasabiQT
  targets the latest Qt (Qt6 today, Qt7+ when it ships) and uses modern
  Qt idioms — no `Q_OBJECT` workarounds, no Win32 type pretence,
  proper Wayland/HiDPI handling, native event paths.

## Status

Bootstrapping. This repo currently has only the README. Subsequent
commits will lay down:

```
public/   — small C++ embedder API (Host interface, Skin loader)
src/      — engine implementation
qt6/      — Qt rendering + event adapter (modern-Qt, ground-up)
tests/    — pixel-regression harness against reference renders
```

First milestone: render WACUP / Winamp Modern's player titlebar
end-to-end with all the right things — text centred, streaks padded
around the sysmenu icon and min/restore/close trio, drawer toggle
working. The same target an earlier in-tree prototype hit before
this repo split out, so its test harness and reference image corpus
transfer directly.

## Targets

- **Linux** — Asahi (aarch64), Fedora, Arch, Debian — Wayland-first.
- **macOS** — Apple Silicon native (M-series), Intel as a courtesy.
- **Windows** — because of course.

Qt6 abstracts the rest. No platform-specific render code beyond what
Qt itself does.

## License

Inspired by but legally distinct from the Llama Group / Winamp 2024
source release. WasabiQT is currently a private effort within the
Winamp-clone ecosystem; licensing is undecided pending downstream
embedder needs.
