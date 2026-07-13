---
title: "Wasabi 2: the API-first framework"
status: active
updated: 2026-07-12
---

# Mission

**qtWasabi is a frontend framework for Winamp-style music players with
full Maki VM support.** Like React to a node server: the player is a
backend service exposing a GraphQL API; qtWasabi connects and renders —
Winamp-modern skins, pixel-faithful, scripted by the unmodified Maki VM,
on desktop (Qt) and in the browser (wasm).

Invariant: **consumers speak ONLY GraphQL; players implement ONLY the
gRPC player protocol.** No player serves GraphQL; no consumer speaks
gRPC. The framework's pylon is the one and only GraphQL server.

# The two contracts

| Contract | Audience | File | Version |
|---|---|---|---|
| GraphQL schema | consumers (heads, wasm, tools, bots, conformance) | `api/schema.graphql` (generated from `api/pylon/src/index.ts`, drift-checked) | 2.0.0 |
| Player protocol | player implementers (C++, Go, Rust, TS) | `api/player.proto` (gRPC over unix socket) | 1.0.0 |

Contract details locked at V0 (breaking to retrofit later):
- **Section masks**: GetState/Command echoes return only requested
  sections; mutations never drag full playlist rows through the pylon.
- **EQ scale**: bands + preamp are −127..127 (Maki scale) end to end;
  0..63 conversion only at the skin-slider edge (the historic 0..63 wire
  double-quantizes Maki EQ steppers into no-ops).
- **Frames**: binary `bytes` on the proto; base64 only at the SSE edge.
  PcmFrames: lossless bounded queue, local-only (pylon refuses it on
  non-local transports). SpectrumFrames produced only while streamed.
- **Art**: streamed chunks (beats the 4 MB unary cap); ETag ≔ artToken.
- **invokeMenuItem carries context** (playlist rows + revision guard,
  library path) from day one.
- **uiExtensions**: contract complete now (pref pages as validated form
  model + menu contributions), implementation deferred until the first
  real player contribution exists.
- Epoch/revision + "position never pushed on a timer" (PositionClock
  interpolation, 5s ping beacon) — carried verbatim from the verified
  remote split, as docstrings in both contracts.

# Pylon v3 (the base — user decision, "wichtig")

Built on github.com/getcronit/pylon branch `feat/v3-fullstack`,
**pinned commit `fbfa23a` + local patch `4a82040`** (branch
`wasabi2/typed-subs` in the ~/git/pylon checkout: async-iterable
passthrough in wrapResolver — enables object-typed subscription
payloads; upstream-PR candidate). Vendored as locally built tarballs;
`typescript` pinned exactly. Subscriptions use the
`experimentalCreatePubSub` form (bare async generators crash the v3
introspector). Serving: TCP via hono serve(), unix socket via
`createServer(getRequestListener(app.fetch)).listen(path)`. Static
wasm-head assets are plain routes (pylon-pages is the react battery,
not needed for that).

# Socket conventions

`$XDG_RUNTIME_DIR/qtwasabi/<name>-<pid>.sock` on Linux (dir 0700);
macOS fallback `$TMPDIR/qtwasabi/` (sun_path ≤ 104 bytes — keep names
short); PER-INSTANCE naming (pid suffix) so two players never collide;
unlink-before-bind; stale-socket caution in tests (wait for
connectability, not existence). Unix socket = filesystem trust; TCP
non-loopback requires the bearer token; public access goes through the
edge (CF) as today.

# Process lifecycle (desktop, orchestrated)

The launcher (qtamp binary) owns the trio: spawn player (gRPC socket)
→ spawn pylon (GraphQL socket, backend socket passed via env) → connect
head. Ownership: children die with the launcher (PR_SET_PDEATHSIG /
process group on Linux; macOS equivalent); pylon respawn with backoff
(audio survives a pylon crash — the head reconnects after respawn);
teardown order head→pylon→player; single-instance + file-association:
a second `qtamp file.mp3` finds the running trio and issues
playlistAdd. Switching the head to a remote player PAUSES the local
player. Kill-pylon-mid-playback is a standing test from V6.

# Node packaging (decided V0)

Fedora RPM: `Requires: nodejs` (system node). macOS: the existing
curl|bash installer provisions node via brew alongside qt. The pylon
ships as built `.pylon/` output + vendored tarballs (no npm at boot).
Packaged-app smoke on both platforms is a V6 gate.

# PROTOCOL.md → contracts mapping (100%)

| PROTOCOL.md (legacy control channel) | Schema v2 / player.proto v1 |
|---|---|
| GET /state (full snapshot) | `Query.player` / `GetState(sections)` |
| snapshot.epoch / revision / serverNowMs | same fields, same semantics (resync rules) |
| transport{playing,paused,positionMs,positionAtMs,durationMs,volume,pan} | `Transport` (+ shuffle/repeat, capability-gated) |
| track{title,artist,album,filename,displayTitle,decoder,bitrate,sampleRate,channels} | `Track` + albumArtist/genre/year/trackNo/disc/composer/publisher/streamGenre + artToken/artUrl |
| playlist{revision,count,currentIndex,rows[{text,durationMs}]} | `Playlist` (+ row.index; rows only in playlistEvents / SECTION_PLAYLIST_ROWS) |
| eq{on,auto,preamp 0..63,bands 0..63} | `Eq` with **−127..127**; auto/preamp now real |
| SSE events state/transport/track/playlist/eq | `playerEvents` kinds STATE/TRANSPORT/TRACK/PLAYLIST_META/EQ + `playlistEvents` (rows) / `Events` stream oneof |
| SSE ping (5s) | `playerEvents` kind PING / `Event.ping` |
| POST /cmd {op,args}: play,pause,stop,next,prev,seekMs,setVolume,setPan | mutations / `Execute(Command)` 1:1 |
| setEqOn,setEqAuto,setEqPreamp(ignored),setEqBand | mutations / ops — preamp NOW honored, Maki scale |
| playlistPlayRow,SetCurrentRow (+expectPlaylistRevision) | same, guards kept |
| playlistAddPaths,RemoveRows,Clear | playlistAdd/Remove/Clear |
| open (musicRoot-confined) | `open` mutation / OpenOp + Capabilities.musicRoot |
| pledit verbs (out of scope v1) | `pleditCommand(verb)` — explicit, capability-gated |
| GET /art/current (ETag=qHash — DRIFT) | pylon `GET /art/current`, **ETag ≔ artToken** (drift resolved); `GetArt` streamed chunks |
| spectrum event ("phase 2", unbuilt) | `spectrumFrames` subscription / SpectrumFrames stream |
| — (new) | capabilities, library, mediaLibrary, uiExtensions, pcmFrames, apiInfo.schemaVersion check |

# V6 gate record (2026-07-13 — step 1)

qtamp goes orchestrated by default: the launcher spawns the player
(`qtamp --serve-player`) and the framework pylon on per-instance unix
sockets, and connects the head over graphql+unix.
- **Orchestrator** (qtWasabi::head, static lib qtwasabi_head): spawns
  player → waits connectability → spawns pylon (player socket via env)
  → waits connectability; children die with the launcher
  (PR_SET_PDEATHSIG); pylon respawns with capped backoff (audio
  survives a pylon crash, the head reconnects on the fresh process);
  a player death is fatal; teardown head→pylon→player. Sockets in
  `$XDG_RUNTIME_DIR/qtwasabi` (0700), pid-suffixed; readiness is
  connectability, never bare existence.
- **Mode selection**: orchestrated is the default for interactive
  launches; `--embedded` restores the in-process player (KEPT until a
  soak passes — V6 step 2 deletes it); `--orchestrated` forces the
  trio even under `--screenshot` (the live-stack gate) and exits 7 on
  failure. All headless test lanes (`--fakehost`, `--connect`,
  `--serve-player`, `--screenshot`, `--probe`, `--list-actions`) keep
  their explicit modes — the six-skin suite still renders embedded and
  fast. **Startup-failure fallback**: if the trio can't start (no
  pylon build, node missing), a default launch falls back to the
  embedded player and keeps running — only `--orchestrated` dies.
- **Single instance**: a QLockFile + a published gsock file; a second
  `qtamp file.mp3` finds the running trio and enqueues (playlistAdd
  over the GraphQL socket) instead of spawning another.
- **Companion RemoteHost**: `setLocalCompanion(true)` advertises
  localFiles and routes openPath/enqueueAndPlay through the protocol
  (`open` / `playlistAddPaths`); the trusted-local musicRoot policy is
  `/` (the launcher-spawned player, same user + machine, accepts any
  local path — the CLI/EJECT/folder/removable-media flows). A
  remote-exposed `--serve-player` still confines to its real
  QTAMP_MUSIC_ROOT. 🔑 pathAllowed had a root=="/" bug (`//` prefix
  rejected every path) — fixed with an unrestricted special case.
- **Gate — the permanent live-stack MAE gate (kept forever)**:
  `tests/remote/orchestrated_test.sh` (ctest `orchestrated`): the
  orchestrated head renders **MAE 0.000 (<3)** vs the baseline over
  the real player→pylon→head trio; single-instance enqueue (1→2 rows,
  no second trio); pylon respawn (head recovers); trio reaped with the
  launcher (PR_SET_PDEATHSIG); startup-failure fallback to embedded
  (stays alive) and `--orchestrated` hard-fail (exit 7). `--embedded`
  byte-identical to the pre-V6 binary; full ctest family green.
- **Step 2 (after soak)**: delete the embedded direct-Host head path.

# V5 gate record (2026-07-12 — complete)

The head framework exists as `qtwasabi_head` (namespace
`qtWasabi::head`), extracted from the reference embedder in gated,
byte-neutral steps:
- **5a HeadChrome**: menu/dialog QSS builders, restyle sweep, Wayland
  popup-grab prep — free functions over the public registries.
- **5b HeadWindow** (3 steps): doc/theme/reload/runtime core with
  SkinQuirks table + injected settings file; subwindow machinery;
  the full input dispatch (Wasabi click protocol, drawer, colour-theme
  list, visMode/timeDisplayMode) with embedder hooks.
- **5c wireRuntime**: the seven engine↔head callback registrations as
  one call.
- **5d HeadMenu**: capability-gated Winamp-parity menu skeleton with
  six extension points (contributeMenu/handleMenuAction/
  showPreferences/skinDocumentChanged/overlayTick/interceptAction),
  stable action-id namespace, EJECT/PLAY-on-empty through the
  capability-gated head picker (engine `Host::pickFile` default is a
  NO-OP now; `HostCapabilities.providesFilePicker` routes picker-owning
  hosts). Gate: menu-tree dump (`WASABIQT_DUMP_MENU`) byte-identical
  across two fixtures + dispatch leg (`WASABIQT_TEST_HEADMENU_PICK`);
  right-click render byte-identical to the pre-extraction binary.
- **5e HeadPreferences**: framework Preferences (default
  `showPreferences`) with the Connection page (backend picker —
  fixed local default, addable remote entries with bearer tokens,
  persisted `[backends]`/`connection/active`; `connectToBackend` hook,
  persist-only until V6 live-switch) and the Presentation page
  (visualization, time display, colour theme — head-local per the sync
  model). Embedder pages via `contributePrefPages`; player pages come
  with uiExtensions later. Token plumbing: the head sends the exact
  `Authorization: Bearer <token>` the pylon gate compares, from
  QTAMP_BEARER_TOKEN or the stored backend entry matching the connect
  URL (wire-asserted by a capture-server test). **Known gap**: the
  wasm GET-subscribe leg cannot send headers (browser EventSource) and
  the pylon has no query-param fallback — wasm heads cannot yet
  authenticate against a token-gated pylon.
- Standing gates through every step: six-skin pixel suite
  byte-identical (both lanes), container_root MAE 1.295, corpus
  fakehost lane, chrome self-test, interaction CLICK_AT A/B against
  pre-extraction binaries, ctest family (now incl. `menu_dump`,
  `headprefs`, `bearer_header`).
- **5f qtwasabi-head** (apps/head): the reference head — FakeHost by
  default, a remote head with --connect (graphql+http(s)/graphql+unix,
  bearer tokens via the shared head::makeTransport), --skin/--container/
  --screenshot/--probe/--list-actions (skin-agnostic action dump),
  HeadShell.qml toplevel (QML window — a C++ QQuickWindow never maps on
  wlroots), the full startup sequence (static well-known scripts → Maki
  runtime in the load-bearing order), resize-relayout wiring,
  SHOT_ALPHA/FORCE_RESIZE/CLICK_AT harness compat. **Gate — the head
  app IS the renderer**: all six embedder pixel baselines render
  BYTE-IDENTICALLY from qtwasabi-head, and the corpus lane run with
  QTAMP=qtwasabi-head produces the embedder lane's exact MAE values
  (20.2/4.5/22.6). head_smoke ctest: baselines + action dump + a LIVE
  orchestrated probe (qtamp --serve-player → pylon on a unix socket →
  head over graphql+unix).

**V5 is complete.** The head framework carries chrome, window core,
input dispatch, runtime wiring, menus, preferences and a reference
application; the reference embedder is a thin branding/player layer
over it.

# V4 gate record (2026-07-12)

The gRPC player protocol is live; the swap was consumer-invisible:
- **qtWasabi::serve::SidecarService** (static lib `qtwasabi_serve`,
  built where grpc++ exists): api/player.proto v1 over `unix:`, backed
  by any PlayerHost. The reference embedder's control-channel server
  logic ported 1:1 (epoch/revision, per-section fingerprints with
  position excluded, apply-time push, playlist revision guard,
  music-root confinement, 5 s ping); Qt-thread marshalling per the V0
  spike patterns. EQ is Maki end to end; GetArt streams chunks;
  Spectrum/Pcm/Library/Ml answer UNIMPLEMENTED until V7/V8.
- **qtamp --serve-player <sock>** replaces `--backend <port>`; the
  hand-rolled backendserver and the legacy HTTP control channel client
  (HttpTransport) are DELETED, along with sync_test/backend_test and
  the legacy wasm mock/cdp lanes. `--connect` accepts
  graphql+http(s)/graphql+unix and plain http(s)/unix — GraphQL is the
  only head data path.
- **Pylon BackendLink is a grpc-js client** (QTAMP_PLAYER_SOCKET;
  proto vendored at api/player.proto, loaded via @grpc/proto-loader):
  GetState sections + Events stream keep the snapshot, commands map
  the historic op vocabulary onto proto Commands, /art/current serves
  from GetArt with ETag == artToken. Track rich fields and Maki EQ
  come typed off the proto (the 0..63 channel conversion died with the
  channel; the C++ GraphQL transport keeps the slider↔Maki edge —
  formula fixed to the inverted mapping, arg key `value`).
- **Gates**: conformance suite (fake-sidecar = FakeHost behind
  SidecarService + probe.mjs over grpc-js: sectioned state, echo
  masks, cold-start play, exact seek, guard, unsupported ops,
  monotonic event revisions, UNIMPLEMENTED streams) — ctest
  `conformance` PASS; grpc==GraphQL equivalence asserted in
  e2e-channel (Maki preamp both sides); e2e-channel (TCP+unix),
  graphql_sync (both GraphQL transports, now a ctest), e2e-remote all
  green on the new stack; six-skin pixel suite byte-identical on both
  lanes; pylon vitest green. wasm rebuild remains bundled with the V3
  server leg (user-gated).

# V3 gate record (2026-07-12)

Moves + schema completion, behavior-neutral + additive:
- **Moves**: `qtWasabi::PlayerHost` (public/qtWasabi/PlayerHost.h, with
  HostCapabilities replacing the deleted isRemote(); analyzerPtr +
  window binding stay in the embedder shim); the remote client family +
  FakeHost live in the framework (public/qtWasabi/remote + src/remote,
  static lib `qtwasabi_remote`, wasm glue symbols `qtwasabi_es_*`);
  their unit tests run under qtWasabi's ctest. The embedder's legacy
  pylon/ dir is gone; PROTOCOL.md sits in the embedder's docs/ with a
  legacy note until the V9 tombstone.
- **Completion**: real EQ preamp end-to-end (the DSP always had the
  axis; channel op + /state + RemoteHost preamp param); EQ auto as
  stored player state; rich track metadata as an additive channel
  `track.meta` object mapped onto the schema's Track fields and served
  through RemoteHost::playItemMetaData; bearer gate on TCP serving
  (loopback open, non-loopback needs PYLON_BEARER_TOKEN, refused
  without one configured); uiExtensions/capabilities/artToken/pledit
  mutations unchanged since V0 (contract-complete).
- 🔑 **EQ scale fix**: the channel wire carries the classic slider
  POSITION scale (0 = top = +12 dB); the old channel↔Maki conversion
  was sign-inverted (boost read as cut) and additionally swallowed
  slider 0 through a `|| 31` falsy default. Both fixed and pinned by
  vitest + the e2e preamp round trip (GraphQL +127 ⇒ channel slider 0).
- Gates: pylon vitest (channelmap + auth) green; e2e-channel (TCP +
  unix, incl. preamp/auto round trip) green; graphql_sync (both
  GraphQL transports) green; e2e-remote (schema v2, random ports)
  green; six-skin pixel suite byte-identical on both lanes; ctest
  family green. **Open**: the wasm head rebuild + cdp gate runs on the
  build server (local x86 builder fails under 16K-page emulation) —
  pending the user-gated server leg.

# V2 gate record (2026-07-12)

The Host vtable is a real seam — the framework gates itself without any
player:
- `qtamp --fakehost` renders against FakeHost, the deterministic
  scripted host (no clocks, no I/O; idle state calibrated to QtampHost).
  Six-skin pixel suite: **byte-identical** to the committed baselines,
  first try; normal QtampHost path unchanged (6/6 both lanes,
  `FAKEHOST=1 tests/regression/run.sh`).
- Corpus lane `EMBEDDER=fakehost tests/corpus/run.sh`: same MAE
  thresholds as the qtamp lane, all PASS.
- Panel-seam fix: MediaLibraryPanel no longer reads playlists/
  bookmarks/history/devices from disk — new additive virtual
  `Host::mlPanelChildren(ns)` (default: empty); QtampHost carries the
  moved file code verbatim, FakeHost serves canned sections. ctest
  `fakehost` covers idle calibration, canned sections, the empty
  engine default, and deterministic interaction.
- Known pre-existing (NOT V2): qtWasabi `layout_test` bit-rotted
  against the Layout API (doesn't compile), `visual_diff` goldens
  stale — both fail identically without the V2 seam; superseded by
  the byte-identical regression + corpus suites.

# V1 gate record (2026-07-12)

The frontend speaks GraphQL — the invariant holds in the product:
- api/pylon fronts the REAL `qtamp --backend` (BackendLink port, typed
  events, CommandResult errors, EQ 63↔Maki interim conversion, art
  sidecar); e2e green over TCP + unix socket.
- GraphQLHttpTransport + GraphQLLocalTransport in qtamp translate the
  channel-shaped RemoteHost calls to the canonical API (RemoteHost
  unchanged); `--connect graphql+http(s)://` and `graphql+unix://`.
- tests/remote/graphql_sync_test.sh: convergence over BOTH transports,
  guard rejection; legacy sync_test stays green; ctest remote family
  green; six-skin pixel suite byte-identical.
- The wasm head speaks GraphQL (EventSource GET-subscribe + POST
  /graphql; glue moved to the RemoteTransport base): 22 MiB, real-
  Chromium cdp gate PASS against the canonical pylon in mock-player
  mode (cdp-check-graphql.mjs; PYLON_STATIC_DIR serves the dist).

# V0 gate record (2026-07-12)

All four spikes PASS (see `spikes/v0/README.md`): pylon v3 typed
subscriptions (with local patch), unix-socket serving + static assets,
C++ QLocalSocket client incl. chunked SSE decoding, gRPC grpc++ 1.48
QObject-host marshalling ↔ grpc-js over `unix:` (macOS leg pending M2
box). schema.graphql v2.0.0 committed (generated; typed subscriptions;
guards live-tested over the unix socket). player.proto v1.0.0 compiles
under protoc 3.19. Milestones + gates: the consolidated plan
(kannst-du-dich-noch-jolly-cake.md); end-product explainer:
~/Dokumente/qtwasabi-wasabi2/wasabi2-endprodukt.pdf.
