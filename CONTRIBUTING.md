<h3 align="center">Contributing to qtWasabi</h3>

<p align="center">The engine renders any skin from one code path. Keep it that way.</p>

qtWasabi is an open-source rendering engine continuing the Wasabi 2 decoupling.
The whole point is that *any* `.wal` skin — including ones that don't exist yet —
renders correctly from the same engine, because the Maki VM is the spec. A few
conventions keep that promise.

### The one rule: fix the engine, never the skin

There is **no per-skin code**. No `if (id == "someWidget")`, no "Bento needs
this", no special-casing a layout. When a skin renders wrong, the bug is in the
engine — a Maki binding returning the wrong value or type, a widget mis-resolving
its rect, an event that doesn't fire — and the fix belongs there, so every other
skin benefits. If you find yourself reaching for a skin name or id, stop: the
real fix is one level down.

The Maki VM itself is **vendored unmodified** (`wasabi-port/`). Don't patch it —
bridge it. Behaviour gaps get fixed in `maki-bindings.cpp` / the widget engine,
not in the VM.

### Build & test

Build per the [README](README.md). The fast verification loop is offscreen
rendering — render a skin to a PNG and diff it:

```
QT_QPA_PLATFORM=offscreen ./build/qtamp --modern-skin "$HOME/.winamp/skins/Bento" \
    --screenshot /tmp/out.png
```

Before sending a change, confirm the three reference skins still render and that
a non-visual change leaves them **byte-identical**:

```
Bento, Big Bento, Winamp Modern  →  0 pixel diff for refactors, 0 Maki gurus
```

A behavioural fix should change *only* what it intends to. If a "general" fix
moves pixels on a skin it wasn't about, it's probably skin-specific in disguise.

### Comments

qtWasabi is its own engine. In code comments:

- **Don't** cite the original Winamp source tree by path (`Src/Wasabi/...`,
  `winamp-linux/...`, `file.cpp:line`). Describe qtWasabi's own behaviour.
- **Do** reference *the Maki VM*, *the Winamp API*, and Wasabi conventions
  (`Wasabi:Frame`, the XML attrs) — those name the spec the engine implements.
  Saying the Maki VM was *open-sourced* is fine; it was.
- **Don't** leave development scaffolding — phase/milestone tags, task numbers,
  "this session", "future work". Explain the *why* of the code as it stands.

Comments explain why, not what. Keep them complete and accurate to the code.

### Where things live

See [ARCHITECTURE.md](ARCHITECTURE.md) for the parse → expand → Maki → paint
pipeline, the embedding boundary (`Host` / `Skin`), and the directory map.

### License

Contributions are under **MIT** (see [COPYING](COPYING)). The Wasabi VM source you
build against carries its own license — your responsibility to honour.
