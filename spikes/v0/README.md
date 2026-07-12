# Wasabi 2 — V0 spikes (all PASS, 2026-07-12)

Scratch code, NOT product. Findings feed `okf/api/` and the V1+ milestones.

## Spike d — Pylon v3 typed subscriptions: PASS (with local patch)

- Pylon v3 = github.com/getcronit/pylon branch `feat/v3-fullstack`,
  **pinned fbfa23a** (+ our local patch, below). Unreleased monorepo;
  packages still report v2-era versions — the PIN is the version.
- Entry authoring changed in v3: `export default new Pylon({graphql})`.
- **Bare async-generator subscription resolvers blow the schema
  introspector** (RangeError: max call stack). Use the
  `experimentalCreatePubSub` + `pubSub.subscribe()` form (the official
  example) — upstream issue candidate.
- Object-typed subscription payloads died at runtime ("Subscription
  field must return Async Iterable. Received: {}"): `wrapResolver` had
  no async-iterable branch and flattened the source. **Fixed by local
  patch `4a82040`** on branch `wasabi2/typed-subs` in ~/git/pylon
  (passes `Symbol.asyncIterator` sources through untouched). With it,
  typed payloads stream with per-subscriber selection sets over POST-SSE
  and GET-SSE (EventSource-compatible). → SCHEMA USES TYPED PAYLOADS.
  Upstream-PR candidate for getcronit.
- Vendoring: tarballs built from the pinned+patched checkout
  (`pnpm run build` at repo root, `pnpm pack` per package). Spike/product
  package.json pins `typescript` exactly (drift caution).

## Spike a — Pylon v3 on a unix socket: PASS

- hono's `serve()` exposes no socket path; the serve PLUGIN uses
  `node:http.createServer(getRequestListener(app.fetch)).listen(path)`.
- Query + typed SSE subscription + a static asset route all work over
  ONE unix socket ($XDG_RUNTIME_DIR/qtwasabi/<name>.sock).
- pylon-pages is the react fullstack battery (docs app); for plain
  static wasm-head assets a simple route/serveStatic suffices — no
  react toolchain needed in the product pylon.
- **SSE responses carry `Transfer-Encoding: chunked`** (node default on
  keep-alive streams) → decided: the C++ client implements chunked
  decoding (spike b), rather than fighting node.

## Spike b — C++ QLocalSocket HTTP/1.1 client: PASS

- `qlocal-client/`: unary POST /graphql (Content-Length response) and an
  UNBOUNDED SSE subscription with an incremental chunked decoder
  (~40 lines), typed frames verified. QNAM cannot AF_UNIX; this is the
  blueprint for `LocalSocketGraphQLTransport` (V1).
- Test-harness gotcha: wait for CONNECTABILITY, not socket existence —
  stale sockets satisfy `-S` before the server binds.

## Spike c — gRPC hardened: PASS (Fedora leg)

- grpc++ **1.48.4** (Fedora 43) + protobuf 3.19: `grpc-spike-server`
  wraps a QObject host living on the Qt MAIN thread:
  - unary GetState: `QMetaObject::invokeMethod(host, …,
    Qt::BlockingQueuedConnection)` from the gRPC thread — clean.
  - Events stream: Qt signal → queued connection → mutex+waitcond queue
    → `ServerWriter::Write` on the gRPC thread — revisions arrive
    strictly increasing at the node client.
- @grpc/grpc-js client over `unix:<path>`: unary + stream both green.
- `api/player.proto` compiles under protoc 3.19 (lowest common
  denominator, incl. proto3 `optional`).
- OPEN: the macOS/brew (current gRPC) leg runs when the M2 box is next
  reachable; wire compatibility is protobuf-guaranteed, the risk is
  build-level only.
