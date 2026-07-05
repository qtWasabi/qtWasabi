---
type: Audit
id: fidelity/window-regions
title: "Window regions: sysregion composition, the visible silhouette, and its consumers"
description: >
  The Win32 SetWindowRgn model qtWasabi must emulate, the three engine
  consumers of sysregion data, and the recurring artifact pair (the
  drawer's extra edge bar / the player's cut-off rounded corner) that
  every wrong emulation produces. Written after the 2026-07-05
  root-cause pass so the next region change starts from the model, not
  from the symptom.
tags: [regions, sysregion, silhouette, drawer, corners, audit]
timestamp: 2026-07-05T12:30:00+02:00
related:
  - layout-geometry.md
  - corpus-status.md
  - crutch-register.md
---

# Window regions

Reference: `winamp-linux/Src/Wasabi/api/skin/widgets/group.cpp`
(`getBaseTextureWindow`, `RenderBaseTexture` under the canvas clip) and
the regionable-widget composition in `api/wnd`.

## The model (what real Winamp does)

On Win32 the skin's window region — composed from `sysregion` /
`regionop` widget contributions — is applied with `SetWindowRgn`, which
shapes **pixels and input at once**. Composition follows the widget
tree in z-order:

- opaque contributions (`sysregion="1"` layers and group rects) ADD
  their pixels to the region as they are reached;
- `sysregion="-N"` mask layers SUBTRACT their opaque mask pixels from
  whatever is composed **so far** — chrome that paints later re-adds
  its own pixels above the cut.

Both directions matter in Winamp Modern's stack (tree order): the
drawer group adds its full-width band, its edge masks
(`drawer.main.left/right.region`) immediately carve the 8px narrowing
strips and the drawer's rounded bottom corners, then `player.main` /
`window.bg2.*` re-add the player band — including the hatched CONFIG
bevel that overlaps the drawer's top rows (y 151–163 at default size) —
and the player's own corner masks (`player.main.*.region`,
`sysregion="-2"`) carve the player's rounded bottom corners last.

Get the order wrong in either direction and you get one of the two
recurring artifacts:

- subtract-too-late / never: the **extra bar** right of the 16K slider
  (the drawer's carved strip stays visible; asymmetric because the
  chrome pre-bake pairs left-anchored fills only with left cutouts);
- subtract-too-globally: the **cut-off rounded corner** at the CONFIG
  bevel (a mask carves chrome that legitimately re-adds above it).

## The three consumers in qtWasabi

| Consumer | Composition | Purpose |
|---|---|---|
| `Layout::computeWindowRegion` | two-pass (all adds, then all subtracts) | INPUT region / `setMask`, `WASABIQT_SHOT_ALPHA` screenshot cut. Over-subtraction is the safe direction here. |
| `Layout::computeVisualRegion` | single **ordered** walk (`RegionPass::Ordered`) | the visible silhouette. `SkinQuickItem::updatePaintNode` clips its paint buffer to this region (skipped during tweens; `WASABIQT_NO_REGION_CLIP=1` opts out). `SkinView::paintEvent` carries the same clip idea for subwindows. |
| `BitmapRegistry::chromeImageFor` pre-bake (`collectChromeCutouts`) | bitmap-level, geometric-overlap pairing, same relat-flags only | bakes cutout alpha into chrome bitmaps so per-bitmap paints carry their shape. Cannot pair a left-anchored fill with a right-anchored (`relatx="1"`) cutout — that gap is why the paint clip is load-bearing. |

Why the paint clip exists at all: `QWindow::setMask` is **input-only on
Wayland** and does not exist in the WebAssembly canvas, so "the OS
window shape will hide it" — the assumption behind retiring the old
final-buffer pass — holds only on X11/Win32. The buffer itself must
carry the silhouette.

## History of this artifact pair (do not re-fix symptoms)

- Per-skin drawer-content centring hardcode → deleted when
  `SOM::makeInt/Float` became type-aware and configtabs.m's `w/2-163`
  evaluated correctly (the "extra bar" generation 1 and 2).
- `d18265b` sysregion=-2 DestinationOut cutouts → `c6efab8` chrome
  pre-bake → `81c8ec3` disabled the legacy final-buffer subtraction
  (it carved the CONFIG bevel: subtract-too-globally) → `3702eb8`
  restored the pre-bake only, relying on setMask for the rest (wrong on
  Wayland/WASM) → `9b437c3` geometric-overlap pairing.
- 2026-07-05: `computeVisualRegion` (ordered walk) + paint-buffer clip
  in `SkinQuickItem`. First attempt clipped to the two-pass INPUT
  region and reproduced 81c8ec3's bevel damage — the two-pass region is
  never a visual.

## Rules

1. The visible silhouette is always the **z-ordered** composition.
   Never clip paint to `computeWindowRegion`.
2. Verify region changes on **raw grabs** (no `WASABIQT_SHOT_ALPHA`) —
   the alpha cut applies the input region and masks exactly the class
   of bug being changed. Check three probe zones on WinampModernPP at
   354 wide: drawer strip rows (y≈200, x 346–353 must be transparent),
   CONFIG bevel (y 151–163, x 346–353 must be opaque with the
   staircase), both drawer bottom corners (rounded).
3. Baselines encode whatever state they were captured in — the strip
   lived in `tests/regression/*.png` through many "green" runs. When a
   region change lands, re-derive the expected diff pixel-exactly
   (restored vs removed counts) before refreshing baselines.
