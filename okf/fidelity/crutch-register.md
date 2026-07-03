---
type: Register
id: fidelity/crutch-register
title: Crutch register — every per-skin hardcode, and what replaces it
description: >
  The authoritative list of skin-specific workarounds in the engine and the
  reference embedder. Each entry names the crutch, why it only works for one
  skin, and the generic mechanism that must replace it. This register only
  shrinks; adding to it requires a matching roadmap entry.
tags: [crutches, hardcode, register, per-skin, debt]
timestamp: 2026-07-03T16:30:00+02:00
related:
  - ../goals/any-skin-fidelity.md
  - maki-vm.md
  - ../roadmap/index.md
---

# Crutch register

A crutch is any code that keys on a specific skin's widget ids, GUID
literals, skin names, or pixel constants instead of deriving behaviour from
the skin's own XML + scripts. Every entry below violates the one goal.
Removal criterion: the generic mechanism ships and the crutch is deleted,
verified across the corpus.

## Embedder (qtamp `src/main.cpp`) — config/AVS drawer complex

| Crutch | Where | Why per-skin | Generic replacement |
|---|---|---|---|
| `setDrawerOpen` reimplements `configtabs.m` OpenDrawer/closeDrawer with ids `player.normal.drawer*`, `drawer.button.*` and pixels y=17/133, compactH=144 | main.cpp:3485 | Bento's drawer geometry, copied into C++ | Run the skin's own drawer script; VM drives `setXmlParam`/`gotoTarget`; delete wholesale |
| `switchDrawerTab` + tab-detection reimplement `configtabs.m::setTabs` on `config.tab.*` ids | main.cpp:3552 | Bento's tab ids | Same: the script flips visibility itself once events + bindings exist |
| `applyDrawerModeFixup` swaps vis/video holder visibility per id | main.cpp:3110 | Papers over the VM holder-lookup gap | Cache/fix WindowHolder script-object lookup so `videoavs.m` show/hide propagates |
| Literal `HeadAMP` skin-name branch + magic tint rect | main.cpp:1164 | Skin name in code | Structural role tagging from widget tree / bitmap alpha |
| 750 ms blanket resize-callback block (suppresses Bento `maximize.m` startup resize) | main.cpp:4116 | Timing tuned to one skin's script | Correct System lifecycle dispatch ordering at load |
| qtamp-private ColorThemes drawer list with fixed pixel metrics | main.cpp:1847 | Bento drawer layout constants | Generic GuiList + skin script drive it |
| `toggleShade` fakes a 30 px strip | main.cpp:3676 | Invented geometry | Switch to the skin's real `<layout id="shade">` |
| Hardcoded window title "Winamp" | main.cpp:4232 | Cosmetic, still a literal | Container/layout title from the skin |

## Engine (`src/`) — layout & chrome

| Crutch | Where | Why per-skin | Generic replacement |
|---|---|---|---|
| `kScriptHiddenByDefault` id list fakes `configtabs.m` initial tab state | Layout.cpp:1228 | Bento drawer ids | Initial visibility from actually running the script at load |
| Static reimplementation of `titlebar.m` resizeObjects | Layout.cpp:1346 | Mirrors known scripts by name | Full Maki dispatch; `runKnownScripts` becomes legacy-only, then deleted |
| `_shift_y` standardframe content shift stamped in C++ | Layout.cpp:677 | Winamp Modern frame constants | `standardframe.maki` owns placement once VM-driven |
| Synthetic menubar fill/divider injected by id-substring | Layout.cpp:422 | Matches `*menubar*` naming | Frame chrome from the skin's own elements |
| `stripNarrowingColumns` aspect-ratio heuristic tuned to Modern's 20x129 drawer masks | Layout.cpp:1709 | Constants from one skin's art | Region data via bound Region/Map classes |
| SIZERWIDTH gap dropped; divider overlaid with Bento-tuned bevel=7 | Layout.cpp:823 | Pixel constant | Real Wasabi:Frame sizer model (reserve 8 px, frame's own v/hbitmap) |
| Auto-shrink-to-painted-alpha window sizing | SkinQuickItem.cpp:373 | Heuristic that fights the Maki resize model; the drawer-clip bug class | Window size purely from layout root w/h + Maki onResize/gotoTarget fixpoint (the recent minimum_h floor is mitigation, not the fix) |
| Volumebar travel=65 / offset=8 in LayerWidget | widgets/Layer.cpp:85 | Winamp Modern volumebar art | Slider semantics from XML attrs + script |
| ColorThemes scrollbar thumb geometry in LayerWidget | widgets/Layer.cpp:24 | Bento constants | One generic ScrollBar widget |
| Dead `_tab_state_key` machinery in Grid (wireTabs no longer plants it) | widgets/Grid.cpp:22 | Orphaned half-crutch | Delete with the tab mechanism unification |

## Engine — state and scripts

| Crutch | Where | Why per-skin | Generic replacement |
|---|---|---|---|
| `privateIntStore` pre-seeded with `winamp5\|DrawerOpen=1`, `winamp5\|ConfigTab=1` | wasabi-port/maki-bindings.cpp:517 | Winamp Modern's section/key names and their startup values baked into the VM store | Seed nothing; `getPrivateInt` returns the caller-supplied default; the skin's `onScriptLoaded` decides its own drawer/tab state |
| Two parallel state systems: `CfgAttribStore` (C++ widgets own toggle/tab/stepper logic) shadows the VM's `privateIntStore` | widgets/Button.cpp:242 | The C++ side reimplements what skins' scripts do on click | One shared store; clicks route to `onLeftClick`/`onAction`, the script writes the attribute, widgets only subscribe and repaint |
| `Config` singleton is a dummy; `newItem/getAttribute/enumItem` are no-ops | wasabi-port/maki-bindings.cpp:1408 | Preference-gated skin logic (a large class) is inert, forcing C++ stand-ins | Real Config service on the same store, with `onValueChanged` dispatch |
| Autowidth font-width heuristics branch on font-family prefixes (`player.`/`drawer.`) | src/Layout.cpp (region/autowidth path) | Prefix naming from one skin family | Measure with the actual resolved font |

## Engine — paint & theming

| Crutch | Where | Why per-skin | Generic replacement |
|---|---|---|---|
| Gammagroup GUESSED by id/tag keywords for untagged bitmaps | BitmapRegistry.cpp:170 | Keyword lists fit known skins | Only tint what carries an explicit gammagroup; theme-less fallback stays opt-in |
| Synthetic color-theme role classifier + kGOW table tuned to Winamp Modern group names | GammasetRegistry.cpp:89 | Name lists | Same confinement to the opt-in fallback |
| Bitmap-font colon heuristics (procedural dots, digit-color sampling, broad timecolonwidth) | TextPainter.cpp:267 | Invented, no reference basis | Uniform glyph path; `timecolonwidth`/`forcefixed` as the Text time-display mode from reference `text.cpp` |
| Fixed Tahoma 11 px in TreeList/MultiColumnList | widgets/TreeList.cpp:94 | Font constant | Skin font via `SWS_USESKINFONT` equivalent |
| MlHostWidget synthesizes several genex colors from literals | ml/MlHostWidget.cpp:95 | Fallback literals | All 24 WADLG slots from the skin's color elements |
| MediaLibraryPanel fixed hand-drawn Bento ML with magic offsets + absolute icon path | widgets/MediaLibraryPanel.cpp:314 | A picture of one skin's window | Retire in favour of the real gen_ml host path (already begun in ml/) |

## Rules

1. This register only shrinks. New crutches do not land.
2. A crutch is removed by shipping its generic replacement and deleting the
   code in the same series, verified on the corpus.
3. Anything discovered later is added here first, then scheduled on the
   [roadmap](../roadmap/index.md).
