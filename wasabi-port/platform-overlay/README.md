# platform-overlay

Port-authored headers that public Wasabi source mirrors don't ship, overlaid
onto `WASABI_SRC_DIR` at configure time (see the top-level `CMakeLists.txt`).

- `Src/replicant/foundation/osx-amd64/types.h` — the macOS platform-type
  variant (below).
- `Src/Elevator/IFileTypeRegistrar_32.h` — a forward-decl stub for a
  Windows-only header Winamp's `Main.h` includes (the real `draw_pe.cpp`
  pledit renderer pulls `Main.h` in). The archive.org source tree ships
  `Src/Elevator/` but not this port stub.

Winamp/Wasabi was only ever built for Windows and, later, Linux. Its
`replicant/foundation/types.h` selects a per-platform `types.h` variant
(`win-amd64/`, `linux-amd64/`, …); no `osx-*` variant exists in any source
mirror. `Src/replicant/foundation/osx-amd64/types.h` here supplies that macOS
variant (Apple Silicon and Intel are both `__LP64__`), letting the Maki VM and
BFC compile on macOS.

This is original port code, not redistributed Wasabi source: nothing here is
copied from the Winamp tree, so it is unaffected by the Winamp Collaborative
License that keeps the rest of the Wasabi source out of this repository.
