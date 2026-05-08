# WasabiQT tests

Two test types live here:

## Unit / functional (Qt Test, C++)

```
*_test.cpp         CMake target; ctest runs them
golden/            committed reference PNG used by titlebar_test
```

| Test                    | Verifies                                             |
|-------------------------|------------------------------------------------------|
| `maki_loader_test`      | M2 — vendored `vcpu.cpp` links + parses `.maki`s    |
| `skin_xml_test`         | M3 — Modern + Bento + Big Bento parse                |
| `layer_paint_test`      | M4 — bitmap registry + sub-rect blits                |
| `titlebar_test`         | M5 — pixel-exact diff vs `golden/titlebar_active_streak.png` |
| `layout_test`           | M6 — groupdef + sendparams expansion                 |
| `tree_paint_test`       | M7 — multi-widget walker on Modern's main/normal     |
| `skinview_test`         | M8 — `SkinView` QWidget pipeline (offscreen grab)    |
| `text_paint_test`       | M10 — bitmap-font glyphs in the timer zone           |

Run them all:

```sh
ctest --test-dir build --output-on-failure
```

## Visual regression (offscreen render)

```
visual/render_layout      — offscreen PNG renderer (no display server!)
visual/compare_visual.sh  — drives render_layout, diffs against expected/
visual/cases.txt          — list of (name, theme, container, layout, w, h, …)
visual/expected/          — committed golden PNGs
visual/actual/            — gitignored; written by the harness
```

The `render_layout` binary is the workhorse — it loads a parsed skin,
runs the full WasabiQT pipeline (parse → expand → paint), and writes
a deterministic PNG using `QT_QPA_PLATFORM=offscreen`.  No window
required; runs anywhere with libQt6Gui (incl. headless CI).

Add a new visual case by appending one line to `cases.txt` (pipe-
separated for spaces in theme names) and re-running the harness with
`WASABIQT_REGEN_GOLDENS=1` to write the golden.

```sh
# Run the visual harness
ctest --test-dir build -R visual_diff --output-on-failure

# Or directly (handy for local diagnosis)
QT_QPA_PLATFORM=offscreen \
    tests/visual/compare_visual.sh \
        build/tests/visual/render_layout \
        tests/visual

# Refresh goldens after a deliberate rendering change
WASABIQT_REGEN_GOLDENS=1 QT_QPA_PLATFORM=offscreen \
    tests/visual/compare_visual.sh \
        build/tests/visual/render_layout \
        tests/visual

# Override skin path (default: ~/.winamp/skins/Winamp Modern/skin.xml)
WASABIQT_TEST_SKIN=/path/to/other/skin.xml ctest --test-dir build -R visual_diff
```

Tolerance is 0.5% mismatched pixels by default — bumped if a future
test needs to absorb minor antialias drift across Qt versions.

The harness is **CI-friendly** — no display server needed.  Wire it up
as e.g. a GitHub Actions step:

```yaml
- run: |
    cmake -B build -DWASABI_SRC_DIR=...
    cmake --build build
    QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```
