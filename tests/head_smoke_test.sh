#!/usr/bin/env bash
# Reference-head smoke (V5f): qtwasabi-head renders FakeHost
# byte-identically to the embedder's committed baselines, dumps a
# non-empty skin-agnostic action list, and probes a LIVE orchestrated
# stack (qtamp --serve-player → pylon on a unix socket → head over
# graphql+unix).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BUILD="${1:-$ROOT/../../build}"
HEAD="$BUILD/qtWasabi/apps/head/qtwasabi-head"
QTAMP="$BUILD/qtamp"
PYLON="$ROOT/api/pylon"
SKINS="$HOME/.winamp/skins"
BASE="$ROOT/../../tests/regression"
[ -x "$HEAD" ] || { echo "no qtwasabi-head at $HEAD"; exit 2; }

fail=0
tmp="$(mktemp -d)"
trap 'kill $BPID $PPID2 2>/dev/null; rm -rf "$tmp"' EXIT
BPID=""; PPID2=""

# 1) FakeHost renders == the reference embedder's committed baselines.
for skin in "WinampModernPP" "Winamp Modern" "DeClassified" \
            "Bento" "Big Bento" "QTAMP-Winamp2000SP4"; do
  slug=$(echo "$skin" | tr ' ' '_' | tr '[:upper:]' '[:lower:]')
  HOME="$tmp" QT_QPA_PLATFORM=offscreen \
    "$HEAD" --skin "$SKINS/$skin" --screenshot "$tmp/$slug.png" \
    >/dev/null 2>&1 || true
  if cmp -s "$BASE/$slug.png" "$tmp/$slug.png"; then
    echo "  OK     $slug byte-identical"
  else
    echo "  FAIL   $slug"; fail=1
  fi
done

# 2) Skin-agnostic action dump is non-empty and well-formed.
ACTIONS=$(HOME="$tmp" QT_QPA_PLATFORM=offscreen \
  "$HEAD" --skin "$SKINS/WinampModernPP" --list-actions 2>/dev/null)
if echo "$ACTIONS" | grep -qi '|play|' && \
   [ "$(echo "$ACTIONS" | wc -l)" -ge 5 ]; then
  echo "  OK     action dump ($(echo "$ACTIONS" | wc -l) widgets)"
else
  echo "  FAIL   action dump"; fail=1
fi

# 3) Live stack: player (gRPC) → pylon (unix) → head (graphql+unix).
if [ -x "$QTAMP" ] && [ -f "$PYLON/.pylon/index.js" ]; then
  PSOCK="$tmp/player.sock"
  export QTAMP_MUSIC_ROOT="$ROOT/../../wasm/assets"
  QT_QPA_PLATFORM=offscreen "$QTAMP" --serve-player "$PSOCK" \
    >/dev/null 2>&1 &
  BPID=$!
  for _ in $(seq 1 100); do [ -S "$PSOCK" ] && break; sleep 0.1; done
  GSOCK="$tmp/gql.sock"
  ( cd "$PYLON" && exec env PYLON_SOCKET="$GSOCK" PORT= \
      QTAMP_PLAYER_SOCKET="$PSOCK" PYLON_DISABLE_TELEMETRY=true \
      node .pylon/index.js >"$tmp/pout" 2>&1 ) &
  PPID2=$!
  for _ in $(seq 1 100); do
    curl -sf --unix-socket "$GSOCK" http://localhost/graphql -X POST \
      -H 'content-type: application/json' \
      -d '{"query":"{apiInfo{schemaVersion}}"}' >/dev/null 2>&1 && break
    sleep 0.1
  done
  PROBE=$(HOME="$tmp" QT_QPA_PLATFORM=offscreen \
    "$HEAD" --connect "graphql+unix://$GSOCK" --probe playing 2>/dev/null)
  if [ "$PROBE" = "false" ] || [ "$PROBE" = "true" ]; then
    echo "  OK     live-stack probe over unix socket ($PROBE)"
  else
    echo "  FAIL   live-stack probe [$PROBE]"; fail=1
  fi
else
  echo "  SKIP   live stack (qtamp or pylon build missing)"
fi

[ $fail = 0 ] && echo "head_smoke: all checks passed" || echo "head_smoke: FAILURES"
exit $fail
