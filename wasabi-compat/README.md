# wasabi-compat

Win32 + Wasabi-API compatibility layer for porting Winamp's `gen_ml` and
its `ml_*` sub-plugins (under `winamp-linux/Src/Plugins/General/gen_ml/`
and `Src/Plugins/Library/ml_*/`) onto qtWasabi.

This sibling-of-`wasabi-port/` exists to make qtWasabi a drop-in
replacement for Win32 + Wasabi at the source level: a plugin's
`#include <windows.h>` / `#include <commctrl.h>` / WASABI_API_* service
queries find Qt-backed implementations here, and the plugin compiles
unmodified (or with a surgical patch in `patches/`).

## Layout

```
wasabi-compat/
├── compat-shim.h         force-included into ported plugin TUs
├── win32/                Win32 API shims (windows.h, commctrl.h, GDI, ...)
├── services/             WASABI_API_* and AGAVE_API_* Qt-backed impls
├── cdio/                 libcdio wrapper used by ml_disc (Phase F)
└── patches/              per-plugin surgical patches (small, < 200 lines)
```

`win32/` provides Win32 base types (HWND, HMENU, HBITMAP, …) as opaque
handles registered in a thread-safe registry. Common-control messages
(LVM_*, TVM_*, HDM_*) route through a SendMessage dispatcher to qtWasabi
widgets (`TreeListWidget`, `MultiColumnListWidget`, `EditWidget`, …).
GDI primitives back onto `QPainter`/`QImage`.

`services/` implements the WASABI/AGAVE service GUIDs gen_ml queries at
init:

- `WASABI_API_SVC` — service manager dispatch table
- `WASABI_API_SKIN` — delegates to qtWasabi `SkinRuntime`
- `WASABI_API_LNG` — string passthrough
- `WASABI_API_SYSCB` — system callbacks (Qt signals/slots)
- `WASABI_API_APP` — app metadata + DPI
- `WASABI_API_PALETTE` — colour queries via skin `ColorRegistry`
- `WASABI_API_WND` — windowing helpers
- `AGAVE_API_DECODE` — file metadata via host's `Host::trackMetadata`
- `AGAVE_API_THREADPOOL` — `QThreadPool`
- `AGAVE_API_CONFIG` — `QSettings`
- `AGAVE_API_MLDB` — `QtSql/SQLite` (Phase G)

`patches/` carries per-plugin surgical fixes for upstream paths that the
shim can't make compile (e.g. `<#ifdef _WIN32>`-only constructs). Each
patch is applied at CMake configure time against a copy of the upstream
source in the build tree; the original `winamp-linux/Src/` is never
modified.

## Build

Built as an OBJECT library `qtwasabi_compat` aggregated into the
top-level `qtwasabi` library. Ported plugin static libs (under
`../plugins/ml_*/`) link `qtwasabi_compat` and force-include
`compat-shim.h`.

See the plan at `~/.claude/plans/happy-tinkering-yao.md` for the phased
rollout (Phase A: scaffolding through Phase G: ml_local + MLDB).
