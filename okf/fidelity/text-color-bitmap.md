---
type: Audit
id: fidelity/text-color-bitmap
title: Text, color, and bitmap fidelity
description: >
  Audit of font metrics, gammaset/tint math, color resolution, and bitmap
  semantics against the reference. The tint core is byte-exact; the largest
  gap is TrueType font sizing, followed by guessed gammagroups and invented
  bitmap-font heuristics.
tags: [text, fonts, color, gammaset, bitmaps, audit]
timestamp: 2026-07-03T16:30:00+02:00
related:
  - crutch-register.md
  - ../roadmap/index.md
---

# Text, color, and bitmap fidelity

## Already byte-exact

`GammasetRegistry::applyToImage` matches Winamp's
`GammaFilter::filterBitmap` exactly (65535+(x<<4), gray max/avg, boost =
channel/2 + alpha/2, per-channel >>16 clamp). XML `<include>` is inlined so
the standard `system-colors.xml` elements reach ColorRegistry and re-tint
correctly under Color Themes.

## Font metrics (high)

- **TrueType sizing** (`TextPainter.cpp:44`): qtWasabi binary-searches a
  pixel size so `QFontMetrics::height() <= fontsize`. The faithful mapping
  is simply `pixelSize = MulDiv(fontsize, 72, 96)` = round(fontsize × 3/4),
  matching both win32 `CreateFontW(-nHeight)` and the Linux XLFD path. The
  search over-sizes titles and LCD digits on every skin whose fontsize is
  far from ~14.
- Default `fontsize` is 12; the reference default is 14
  (`TextPainter.cpp:320`).
- Hardcoded +2 left inset ignores `leftpadding`/`rightpadding`
  (`TextPainter.cpp:418`).

## Bitmap fonts (medium)

- Colon handling is invented (procedural dots, digit-color sampling,
  blank-colon detection, broad `timecolonwidth`), with no reference basis
  (`TextPainter.cpp:267`). `timecolonwidth`/`forcefixed` are a time-display
  MODE of the Text widget in reference `text.cpp`, not glyph heuristics.
- Advance/inset diverges from `do_textOut`: missing `hor_spacing/2` lead-in
  inset and trailing spacing (`TextPainter.cpp:288`).

## Color and theming (medium)

- Untagged bitmaps get gammagroups GUESSED from id/tag keywords
  (`BitmapRegistry.cpp:170`); real Wasabi never guesses. Tint only what
  carries an explicit gammagroup; keep the synthetic recolor
  (kGOW/roleOfGroup, `GammasetRegistry.cpp:89`) confined to the opt-in
  theme-less fallback.
- Uncolored/literal `r,g,b` text skips the active gammaset and there is no
  default "Text" colorgroup (`ColorRegistry.cpp:104`).
- Color tokens run the `filterBitmap` math instead of the shipped
  `filterColor` (red/blue multiplier swap, 65536, boost+128)
  (`ColorRegistry.cpp:55`).

## Bitmap semantics (low)

- No magenta-colorkey / region-map transparency for classic BMP assets the
  font/bitmap paths half-support (`FontRegistry.cpp:146`).
