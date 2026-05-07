# WasabiQT

A Qt-native skin-engine harness for Winamp's **Wasabi** widget tree
and **Maki** bytecode VM. WasabiQT is the modern Qt6+ rendering and
event glue; the actual VM and widget classes come from the opensourced
Wasabi source release (Llama Group, 2024), which you supply yourself.

Pronounced "wasabi-cute".

## What's in this repo, what isn't

**In this repo:**
- `qt6/` — modern-Qt rendering and event adapter (our code)
- `src/` — integration glue, host interface, skin loader entry points (our code)
- `public/` — small C++ API for embedders (our code)
- `tests/` — pixel-regression harness against canonical reference renders (our code)
- `cmake/` — build glue that locates a user-supplied Wasabi source tree

**Not in this repo, never will be:**
- `Src/Wasabi/api/` — the original Wasabi widget classes + Maki VM
- `Src/Wasabi/bfc/` — Wasabi's foundation framework
- Anything else from the Winamp source release

You provide that yourself. WasabiQT's CMake configuration takes a
`WASABI_SRC_DIR` argument (or env var) pointing at your local copy of
the opensourced `Src/` tree, and builds against it.

## How to build

```bash
# 1. Get the opensourced Winamp source release. It's been mirrored on
#    archive.org since 2024; multiple copies are available. Either
#    extract it into ~/winamp-src or wherever you prefer.
#
# 2. Clone WasabiQT.
git clone https://github.com/.../WasabiQT
cd WasabiQT

# 3. Configure pointing at your local Wasabi source tree.
cmake -B build -DWASABI_SRC_DIR=$HOME/winamp-src/Src

# 4. Build.
cmake --build build
```

If `WASABI_SRC_DIR` is unset or points at an incomplete tree, CMake
fails with a clear message listing the subdirs it needs (`api/script`,
`api/wnd`, `api/skin/widgets`, `bfc`, `Lib`).

## Why this split

The Wasabi source is licensed under the **Winamp Collaborative License
v1.0**, which restricts redistribution to the official Winamp maintainers
— even unmodified copies cannot legally be conveyed by anyone else. So
WasabiQT can't embed it.

But the source is publicly archived: anyone can download it themselves,
and the moment they do, they have the right to run it under WCL §3
("propagate Covered works that you do not Convey"). They then point
WasabiQT's build at their local copy. The conveyance happens between
archive.org and them, not via WasabiQT.

This pattern is well-established for legally-restricted but publicly-
available code: console emulators expecting user-supplied BIOS files,
old DirectX wrappers expecting an externally-installed SDK, etc.
WasabiQT itself stays freely distributable while delivering full
skin-engine compatibility through the original unmodified VM.

## Why use the original VM rather than reimplementing it

Thousands of shipped Modern-era Winamp skins exercise small quirks of
the original Maki VM and the surrounding widget classes — text-widget
left insets, padding accumulators, group-instance resolution, alignment
defaults, sendparams scoping, coordinate-conversion chains. Each is
documented only in the running code. A clean-room re-implementation
becomes a years-long bug-hunt.

The original Winamp team, when planning **Wasabi 2** (`Src/replicant/`),
made the same call: keep the VM and widget classes unchanged, modernise
only the service plumbing. WasabiQT goes further — modernise the
*rendering and event* layer to Qt6 — but keeps the VM unchanged for
exactly the same reason.

## Inspirations

WasabiQT is inspired by, but legally distinct from:

- **Wasabi 1** — the original widget framework and Maki VM. We use it
  unmodified through the build path described above; we do not ship
  it.
- **Wasabi 2** (`Src/replicant/`) — the never-completed cross-platform
  service rewrite. WasabiQT shares its goal of cross-platform Wasabi
  with skin-format compatibility intact.
- **`Src/Wasabi/qt6/QtWindowAdapter` / `QtCanvasAdapter`** (~2015) —
  the abandoned Qt6 adapter shim. Useful as **reference** for what
  surface to expose to Wasabi's expectations, but the implementation
  there targets a 2015-era Qt with several Q_OBJECT / MOC workarounds
  no longer needed. WasabiQT targets the **latest Qt** (Qt6 today,
  Qt7+ when it ships) with modern idioms — Wayland-first event paths,
  HiDPI awareness, native QPainter rendering, no Win32-type pretence
  in the public API.

## Why this matters to me

The dream is selfish: I want to listen to music on my Apple M-Series
Mac the way I did on Windows in the early 2000s — Winamp running
natively on **Asahi Linux** and **macOS**. No Wine, no x86 emulation,
no nostalgia-VM in a window. Just the actual skin engine, on aarch64
silicon, painted by the same Qt that already gives me a beautiful
desktop on Asahi.

Beyond that: WasabiQT is meant to be **embeddable in any themeable
Winamp clone**. Audacious, WACUP, the next thing, and the thing after
that. Wasabi was a great UI framework that nobody else could use because
it shipped welded to one player's runtime. WasabiQT is the small,
embeddable piece you actually wanted — a Qt skin engine that doesn't
care which player owns the playback state, the playlist, or the
service registry. Implement a small `Host` interface, you have skins.

## Targets

- **Linux** — Asahi (aarch64), Fedora, Arch, Debian — Wayland-first.
- **macOS** — Apple Silicon native (M-series), Intel as a courtesy.
- **Windows** — because of course.

Qt6 abstracts the rest. No platform-specific render code beyond what
Qt itself does.

## Status

Bootstrapping. The repo currently has only this README. Subsequent
commits will add the CMake glue, the qt6 adapter (re-derived for
modern Qt rather than ported from the 2015 stub), the embedder API,
and a regression harness whose first target is rendering the WACUP /
Winamp Modern player titlebar correctly through the unmodified VM.

## License

WasabiQT's own code (everything actually in this repo) will be released
under a permissive open-source licence to be decided before first
public release. The Wasabi source it links against is **not in this
repo**; that source carries the Winamp Collaborative License and you
obtain it separately.
