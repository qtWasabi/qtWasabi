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
