---
type: Audit
id: fidelity/corpus-status
title: "Corpus status: per-skin render fidelity against reference screenshots"
description: >
  The living record of which corpus skins render faithfully. Two
  reference classes: the skin authors' own published screenshots
  (github.com/qtamp forks) and captures of REAL Winamp (tests/refs in
  the reference embedder, RAM album loaded). Measured by
  tests/corpus/run.sh; updated 2026-07-04 after the first two fix
  batches.
tags: [corpus, skins, fidelity, regression, audit]
timestamp: 2026-07-04T22:30:00+02:00
related:
  - index.md
  - ../roadmap/index.md
---

# Corpus status

The corpus harness (`tests/corpus/run.sh`) renders every corpus skin
through the reference embedder DURING PLAYBACK, hermetically (sandboxed
HOME), with the window-region mask applied and transparent margins
trimmed, then compares size and mean absolute RGB error against the
reference capture. `manifest.tsv` carries the per-skin budget, display
render ratio and reference media.

**Hermetic or it did not happen:** a stored user color theme
(`colortheme=Synthetic: ...` in winamp.conf) once masqueraded as an
engine-wide color defect during ad-hoc renders. Every fidelity
measurement must run through the harness, never from a developer
profile.

## Status (2026-07-04, after batches 1-3)

| Skin | Reference | Status | Notes |
|---|---|---|---|
| [WinampModernPP](https://github.com/qtamp/WinampModernPP) | author screenshot | **faithful** | Pixel-identical through all engine changes; the marketing/press skin. |
| [winamp1](https://github.com/qtamp/winamp1) | author screenshot | **faithful (MAE 4.4)** | Was 354x106 garbage; fixed by AUTOWH layout sizing plus the layout-root background fill. |
| [DeClassified](https://github.com/qtamp/DeClassified) | author screenshot | **passing (MAE 20.7)** | Classic analyzer lights up since the engine owns transport events; ticker uses the classic "N. title (M:SS)" format; balance strip folds around centre. Residual MAE is track-content difference plus known asset drift. |
| [Winamp2000SP4](https://github.com/qtamp/Winamp2000SP4) | author screenshot | in progress | Render-ratio model landed (2x-authored art, reference captured at 50%); still 4px tall: the window-region mask is not yet scaled by the ratio. Then: truetypefont registry, Win9x gradient widget, SApplication caption bindings, getVisBand. |
| Bento | REAL Winamp (RAM album) | **passing (MAE 23.4)** | Playlist durations now come from the tag headers and match the reference; fileinfo lines render the reference's pale blue-gray. Remaining line items: the Play control is not the reference's split button+arrow, its menu popup does not open (gen_ml menu, the visible dropdown work item), Browser tab absent. |
| WinampModern | REAL Winamp (RAM album) | not scoreable yet | The reference is a MULTI-WINDOW session capture (player, playlist editor and media library arranged separately); the harness renders one window. Disabled in the manifest until it can arrange subwindows or a player-only reference exists. The forced-width render also shows the config drawer undocked, an RB1 frame-constraint symptom to fix regardless. |

## Reading the failures

Every confirmed divergence traces to an engine dimension, never to a
skin: the DropDownList placeholder is the RB6 widget-completeness item,
the transport events were the event-surface gap (now closed), the
sizing failures were the AUTOWH / background-fill / render-ratio gaps
in [layout-geometry](layout-geometry.md). The four historical corpus
symptoms fixed so far each removed a whole class, not a skin.

## Process

1. References live in `tests/golden/corpus/ref-<skin>.png`; author
   screenshots come from the upstream repos, real-Winamp captures from
   the reference embedder's `tests/refs/`.
2. `QTAMP=<embedder> tests/corpus/run.sh [skin]` renders and scores.
3. Divergences are root-caused to an engine dimension and filed in
   [the audit](index.md); never patch the skin.
4. A skin turns **faithful** only when the harness passes at its
   reference size and budget.
