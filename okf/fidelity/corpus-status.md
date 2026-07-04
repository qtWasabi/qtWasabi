---
type: Audit
id: fidelity/corpus-status
title: Corpus status — per-skin render fidelity against author references
description: >
  The living record of which corpus skins render faithfully and which are
  broken, verified against the skin authors' own reference screenshots.
  Started 2026-07-04 with the five QTAMP showcase forks of 0x5066's MIT
  skins. Feeds the corpus-verification workstream.
tags: [corpus, skins, fidelity, regression, audit]
timestamp: 2026-07-04T05:10:00+02:00
related:
  - index.md
  - ../roadmap/index.md
---

# Corpus status

The showcase forks (github.com/qtamp) double as the first regression
corpus. Verification standard: the skin author's own published reference
screenshots, not "looks plausible".

## Status (2026-07-04, offscreen render, Linux)

| Skin | Status | Notes |
|---|---|---|
| [WinampModernPP](https://github.com/qtamp/WinampModernPP) | **faithful** | Matches the author's reference: titlebar, LCD display, songticker, drawer open/close, EQ panel, tabs, playlist editor all correct. The only corpus skin cleared for marketing/press shots. |
| [DeClassified](https://github.com/qtamp/DeClassified) | broken | Renders recognizably (classic-skin frame, transport, display) but diverges from the author's reference in detail; needs a per-element diff. |
| [winamp1](https://github.com/qtamp/winamp1) | broken | Large blank regions; controls render but the sparse layout diverges from the reference; unlabeled checkbox art suggests missing element resolution. |
| [Winamp2000SP4](https://github.com/qtamp/Winamp2000SP4) | broken | Win9x chrome renders, but the titlebar text is missing (skin bitmap font `titlebar`/`vgasys` not rendering) and the LCD shows ghost `88:88` segments. |
| [Winamp3x](https://github.com/qtamp/Winamp3x) | broken | Worst of the set: collapsed layout, missing text throughout (same bitmap-font family as Winamp2000SP4), partial chrome only. |

## Reading the failures

The three broken Zsolt Vajda/Victor Brocaz-lineage skins share symptoms
pointing at known audit findings rather than new classes: skin-supplied
bitmap fonts that never render (see
[text-color-bitmap.md](text-color-bitmap.md), bitmap-font advance/lookup
divergences and the missing classic-BMP colorkey path), and layouts that
collapse where scripts drive geometry the VM cannot yet execute (see
[maki-vm.md](maki-vm.md)). Each broken skin must be root-caused to an
engine defect and fixed generically; the corpus table then flips per skin.

## Process

1. Collect the author's reference screenshots per skin (0x5066 publishes
   them in each repo's README).
2. Offscreen-render the same views and diff.
3. File each divergence against the responsible engine dimension in
   [the audit](index.md); never patch the skin.
4. A skin turns **faithful** only when the diff is clean at native size.
