#!/usr/bin/env bash
# Proto-level conformance: fake-sidecar (FakeHost + SidecarService)
# probed over grpc-js.  No player, no pylon, no GraphQL — this is the
# player-contract gate every implementation must pass.
#
#   tests/conformance/run.sh [path/to/fake-sidecar]
#
# node modules come from api/pylon (the pylon depends on grpc-js).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
BIN="${1:-$ROOT/../../build/qtWasabi/tests/fake-sidecar}"
PYLON="$ROOT/api/pylon"

[ -x "$BIN" ] || { echo "no fake-sidecar at $BIN (build with grpc++)"; exit 2; }
[ -d "$PYLON/node_modules/@grpc/grpc-js" ] || {
    echo "grpc-js missing (cd api/pylon && npm i)"; exit 2; }

tmp="$(mktemp -d)"
SOCK="$tmp/fake.sock"
MUSIC="$tmp/music"
mkdir -p "$MUSIC"
touch "$MUSIC/fake.mp3"
trap 'kill $SPID 2>/dev/null; rm -rf "$tmp"' EXIT

QT_QPA_PLATFORM=offscreen "$BIN" "$SOCK" "$MUSIC" >"$tmp/out" 2>&1 &
SPID=$!
for _ in $(seq 1 100); do [ -S "$SOCK" ] && break; sleep 0.1; done
[ -S "$SOCK" ] || { echo "fake-sidecar never came up"; cat "$tmp/out"; exit 1; }

# ESM resolves imports from the module's own directory — link the
# pylon's node_modules next to the probe.
ln -sfn "$PYLON/node_modules" "$HERE/node_modules"
node "$HERE/probe.mjs" "$SOCK" "$ROOT/api/player.proto" "$MUSIC"
exit $?
