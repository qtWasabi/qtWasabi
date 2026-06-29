<h3 align="center">qtWasabi — Architecture</h3>

<p align="center">How a <code>.wal</code> skin becomes pixels, and where the seams are.</p>

qtWasabi is a **rendering engine**, not a player. It loads a Wasabi skin,
runs the skin's own Maki scripts on the open-sourced Maki VM, and paints the
result through `QPainter`. Everything player-shaped — playback, the mixer,
metadata, the playlist, the library — it asks for through one interface
(`qtWasabi::Host`) that the embedder implements. That single seam is what
makes the engine standalone: it continues the Wasabi 2 decoupling by *never*
talking to a player directly.

### The pipeline

A skin goes through one direction, parse → pixels:

```
skin.xml ──▶ SkinXml ──▶ Layout ──▶ Widget tree ──▶ SkinRuntime ──▶ Painters ──▶ Qt surface
            (parse)   (expand)   (resolve rects)   (Maki VM)      (QPainter)   (SkinView)
```

| Stage | Lives in | Does |
|---|---|---|
| **Parse** | `SkinXml.{h,cpp}` | Read `skin.xml` + its includes into a `Document`: containers, layouts, groupdefs, `<script>` refs, bitmaps, colors, fonts, gammasets. |
| **Expand** | `Layout.{h,cpp}` | `expandLayout(doc, container, layout)` inlines every `<groupdef>`, applies `<sendparams>` overrides per group instance, and emits a tree of `Widget` nodes. Also models `Wasabi:Frame` pane splits, the `sysregion` window mask, and chrome cut-outs. |
| **Widget tree** | `Widget.{h,cpp}`, `src/widgets/*` | Polymorphic widget classes (`LayerWidget`, `ButtonWidget`, `SliderWidget`, `TextWidget`, `ContainerWidget`, `WindowHolderWidget`, …). Each resolves its rect from attrs (including relative `relatw`/negative-`w` coords) at paint time and alpha-hit-tests for input. |
| **Maki VM** | `SkinRuntime.{h,cpp}` + `wasabi-port/` | Loads each `<script>` into the VM, binds a script object to every id'd widget, and dispatches the skin's handlers — `onScriptLoaded`, `onResize`, `onAction`, `onTimer`, `setXmlParam`, `findObject`, … The scripts drive the layout; the engine just runs them. |
| **Paint** | `TreePainter`, `LayerPainter`, `TextPainter` | Walk the tree and paint each widget through `QPainter`. The `BitmapRegistry` / `FontRegistry` / `ColorRegistry` / `GammasetRegistry` resolve skin resources. |
| **Surface** | `SkinView` / `SkinQuickItem`, `qt6/` | The Qt integration — a `QWidget` / `QQuickItem` that hosts the painted tree, routes mouse + resize events back into the engine, and applies the alpha window mask. |

### The Maki VM seam

The Maki bytecode VM is **vendored unmodified** — it is the one part qtWasabi
deliberately does not re-implement, because thousands of shipped skins are the
only real spec for it. It is bridged, never edited:

- **`wasabi-port/`** — the open-sourced Maki VM (vcpu, scriptmgr, objecttable,
  scriptobj) plus the minimal BFC subset it needs. `maki-bridge.cpp` is the
  *only* translation unit that includes the VM's headers, so its types never
  leak into the Qt side.
- **`maki-bindings.cpp`** — the `wq_*` method bodies the VM calls when a script
  does `setXmlParam`, `getAutoWidth`, `findObject`, `new Timer`, etc. These hold
  Maki/Wasabi semantics (e.g. a `Timer`'s `setDelay` only re-arms when already
  started; `getPosition` on a frame returns its live divider position).
- **`SkinRuntimeBridge.cpp`** — a small `extern "C"` surface that lets the
  bindings reach the Qt widget tree (look up a widget by id, mutate an attr,
  fire an event) without including any Qt header.

The rule everywhere here: **fixes are engine-level, never per-skin.** There is
no `if (id == "someWidget")` glue. When something renders wrong, the fix lands
in the VM bindings or the widget engine so that *every* `.wal` skin benefits
from the same code path. A good example is layout settling: when a script
mutates geometry in an `onResize` handler, the engine re-runs the onResize
cascade to a fixpoint — exactly the reflow the scripts expect — instead of
hard-coding any one skin's result.

### The embedding boundary

Two headers are all an embedder touches:

```cpp
class MyPlayer : public qtWasabi::Host { /* ~40 virtuals: playback, mixer,
                                            metadata, playlist, library, … */ };

MyPlayer host;
qtWasabi::Skin skin(&host);
skin.load("/path/to/Bento");        // a .wal archive or an unpacked dir
window->setCentralWidget(skin.widget());
```

- **`Host.h`** — the player bridge. Position / duration / play-state, transport
  (`play`/`pause`/`next`/seek), mixer (`volume`, per-action slider values),
  metadata (`songTitle`, `playItemMetaData`), spectrum / VU data, album art,
  and the playlist + media-library row models. Most methods have safe defaults,
  so a minimal player overrides only a handful.
- **`Skin.h`** — load a skin, get a `QWidget`. That's the whole entry point.

Because the engine only ever reaches the outside world through `Host`, any Qt
player — Audacious, WACUP, something new — gets classic skin support by
implementing that interface. The reference embedder is **qtamp**, the player in
this repo.

### Plugin compatibility (`wasabi-compat/`)

The original media-library is a set of `ml_*` plugins from the gen_ml lineage.
Rather than re-implement them, `wasabi-compat/` provides a Win32 + Winamp-API
shim (window messages, GDI raster, the service manager, `wa_dlg` theming) so
those plugins compile and run on Linux against the engine. This is how the real
media-library window renders, themed by the skin, with no re-implementation.

### Directory map

| Path | Contents |
|---|---|
| `public/qtWasabi/` | The public API — `Host`, `Skin`, `SkinView`, `SkinRuntime`, `Widget`, the resource registries. |
| `src/` | Engine implementation — `widgets/`, the painters, `Layout`, the runtime bridge, `ml/` (media-library window), `pledit/` (in-player playlist). |
| `wasabi-port/` | The vendored Maki VM + BFC subset, and the bridge / bindings that drive it. |
| `wasabi-compat/` | The Win32 / Winamp-API shim that runs original `ml_*` plugins. |
| `qt6/` | Qt window + canvas integration. |
| `tests/` | Loader, parser, layout, and visual-render regression tests. |

### What's deliberately *not* here

- **No platform-port layer.** The open-sourced BFC platform pieces (`std_file`,
  `std_wnd`, the X11 backend) are not used; Qt provides all of it.
- **No per-skin code.** See above — the Maki VM is the spec.
- **No player.** Playback, decoding, and the music library live behind `Host`,
  in the embedder.
