#!/usr/bin/env bash
# V1a e2e: real `qtamp --backend` (legacy channel) fronted by the
# canonical qtwasabi-pylon serving schema v2 — checks over TCP AND the
# unix socket: snapshot mapping, mutation round trip, revision guard
# rejection as CommandResult.error, typed playerEvents push.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
QTAMP="${QTAMP:-$HERE/../../../../build/qtamp}"
[ -x "$QTAMP" ] || { echo "no qtamp at $QTAMP"; exit 2; }
FIXDIR="$(cd "$HERE/../../../../wasm/assets" 2>/dev/null && pwd)"
export QTAMP_MUSIC_ROOT="${FIXDIR:-/tmp}"

fail=0
contains() { if echo "$1" | grep -q "$2"; then echo "  ok   $3"; else echo "  FAIL $3: [$2] not in [$1]"; fail=1; fi; }

tmp="$(mktemp -d)"
BPID=""; PPID_PYLON=""
trap 'kill $BPID $PPID_PYLON 2>/dev/null; rm -rf "$tmp"' EXIT

QT_QPA_PLATFORM=offscreen "$QTAMP" --backend 0 >"$tmp/bout" 2>"$tmp/berr" &
BPID=$!
BPORT=""
for _ in $(seq 1 100); do
  BPORT="$(sed -n 's/.*listening on 127.0.0.1:\([0-9]*\).*/\1/p' "$tmp/berr" | head -1)"
  [ -n "$BPORT" ] && break; sleep 0.1
done
[ -n "$BPORT" ] || { echo "backend never came up"; exit 1; }
echo "backend :$BPORT"

EPORT=$((18900 + RANDOM % 100))
SOCK="${XDG_RUNTIME_DIR:-/tmp}/qtwasabi/e2e-channel-$$.sock"
rm -f "$SOCK"
( cd "$HERE" && exec env PORT=$EPORT PYLON_SOCKET="$SOCK" \
    QTAMP_BACKEND_URL="http://127.0.0.1:$BPORT" \
    PYLON_DISABLE_TELEMETRY=true \
    node .pylon/index.js >"$tmp/pout" 2>&1 ) &
PPID_PYLON=$!
for _ in $(seq 1 60); do
  curl -sf http://127.0.0.1:$EPORT/graphql -X POST -H 'content-type: application/json' -d '{"query":"{apiInfo{schemaVersion}}"}' >/dev/null 2>&1 && break
  sleep 0.25
done

GQL() { curl -s "$@" -H 'content-type: application/json'; }

SNAP="$(GQL http://127.0.0.1:$EPORT/graphql -d '{"query":"{player{epoch revision transport{volume playing} eq{preamp bands}} capabilities{playlistEdit}}"}')"
contains "$SNAP" '"volume":70' "TCP: snapshot maps channel transport (volume 70)"
contains "$SNAP" '"playlistEdit":true' "TCP: capabilities"

USNAP="$(GQL --unix-socket "$SOCK" http://localhost/graphql -d '{"query":"{player{revision transport{volume}}}"}')"
contains "$USNAP" '"volume":70' "unix: same snapshot over the socket"

ADD="$(GQL http://127.0.0.1:$EPORT/graphql -d "{\"query\":\"mutation{playlistAdd(paths:[\\\"$QTAMP_MUSIC_ROOT/demo.wav\\\"]){ok revision player{playlist{rowCount rows{index text}}}}}\"}")"
contains "$ADD" '"ok":true' "mutation playlistAdd ok"
contains "$ADD" '"rowCount":1' "mutation echo carries refreshed playlist"

GUARD="$(GQL http://127.0.0.1:$EPORT/graphql -d '{"query":"mutation{playRow(row:0,expectPlaylistRevision:999){ok error}}"}')"
contains "$GUARD" '"ok":false' "revision guard rejects as CommandResult"
contains "$GUARD" 'ismatch' "guard error message surfaces"

# EQ: preamp is real end-to-end on the Maki gain scale (+127 = boost =
# channel slider 0); auto is stored player state.
PRE="$(GQL http://127.0.0.1:$EPORT/graphql -d '{"query":"mutation{setEqPreamp(value:127){ok player{eq{preamp}}}}"}')"
contains "$PRE" '"preamp":127' "setEqPreamp round-trips on the Maki scale"
RAWPRE="$(curl -s "http://127.0.0.1:$BPORT/state" | grep -o '"preamp":[0-9]*')"
contains "$RAWPRE" '"preamp":0' "channel carries slider scale (Maki +127 = slider 0)"
AUTO="$(GQL http://127.0.0.1:$EPORT/graphql -d '{"query":"mutation{setEqAuto(on:true){ok player{eq{auto}}}}"}')"
contains "$AUTO" '"auto":true' "setEqAuto stores and reports"

(sleep 0.6; GQL http://127.0.0.1:$EPORT/graphql -d '{"query":"mutation{play{ok}}"}' >/dev/null) &
SUB="$(timeout 3 curl -sN --unix-socket "$SOCK" http://localhost/graphql -X POST \
  -H 'content-type: application/json' -H 'accept: text/event-stream' \
  -d '{"query":"subscription { playerEvents { kind revision transport { playing } } }"}' | grep 'data:' | head -8)"
contains "$SUB" '"playing":true' "typed playerEvents push over unix socket (play)"

if [ "$fail" = 0 ]; then echo "e2e-channel: all checks passed"; else echo "e2e-channel: FAILURES"; cat "$tmp/pout" | tail -5; fi
exit $fail
