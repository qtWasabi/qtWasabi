#!/usr/bin/env bash
#
# Corpus fidelity test: render each corpus skin through the reference
# embedder and measure it against the SKIN AUTHOR'S published
# screenshot, not against our own past output. This is the executable
# form of the one goal ("thousands of shipped skins are the spec") and
# of okf/fidelity/corpus-status.md.
#
# A skin passes when
#   1. the rendered window size equals the reference size exactly, and
#   2. the mean absolute RGB error (MAE) against the reference is at or
#      below the skin's threshold in manifest.tsv.
# Dynamic regions (song ticker, time display, analyzer) differ by
# nature, so thresholds are calibrated per skin rather than demanding
# pixel equality; the window chrome and layout dominate the area.
#
# Usage:
#   tests/corpus/run.sh                 # evaluate all corpus skins
#   tests/corpus/run.sh winamp1         # evaluate one skin
#   QTAMP=/path/to/qtamp SKIN_DIR=~/.winamp/skins tests/corpus/run.sh
#
# The reference screenshots live next to this script's manifest under
# ../golden/corpus/ref-<skin>.png and come from the skins' upstream
# repositories (github.com/qtamp/<skin>, MIT, by their authors).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENGINE_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
REF_DIR="$ENGINE_ROOT/tests/golden/corpus"
QTAMP="${QTAMP:-$ENGINE_ROOT/../../build/qtamp}"
SKIN_DIR="${SKIN_DIR:-$HOME/.winamp/skins}"
MANIFEST="$SCRIPT_DIR/manifest.tsv"
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

if [[ ! -x "$QTAMP" ]]; then
    echo "error: reference embedder not found at $QTAMP (set QTAMP=...)" >&2
    exit 2
fi

only="${1:-}"
fail=0
printf "%-18s %-11s %-9s %-7s %s\n" "skin" "size" "mae" "limit" "result"

while IFS=$'\t' read -r skin dir mae_limit ratio; do
    ratio="${ratio:-1}"
    [[ "$skin" =~ ^# ]] && continue
    [[ -n "$only" && "$skin" != "$only" ]] && continue
    ref="$REF_DIR/ref-$skin.png"
    if [[ ! -f "$ref" ]]; then
        printf "%-18s %s\n" "$skin" "SKIP (no reference)"
        continue
    fi
    if [[ ! -d "$SKIN_DIR/$dir" ]]; then
        printf "%-18s %s\n" "$skin" "SKIP (skin not installed: $SKIN_DIR/$dir)"
        continue
    fi

    shot="$TMPDIR/$skin.png"
    # The author screenshots show the players DURING playback (lit
    # analyzer, ticker, kbps/kHz boxes, play-state chrome), so render
    # in the same state: the bundled test tone starts playing at boot.
    # HOME points at the sandbox so the run is hermetic: the user's
    # winamp.conf (vis mode, saved skin, volume...) must not leak into
    # fidelity measurements.  SKIN_DIR was expanded above, so skins
    # still come from the real location.
    HOME="$TMPDIR" WASABIQT_RENDER_RATIO="$ratio" \
        WASABIQT_SHOT_ALPHA=1 \
        QT_QPA_PLATFORM=offscreen "$QTAMP" \
        --modern-skin "$SKIN_DIR/$dir" \
        "$SCRIPT_DIR/testtone.wav" \
        --screenshot "$shot" >"$TMPDIR/$skin.log" 2>&1 || true

    if [[ ! -f "$shot" ]]; then
        printf "%-18s %-11s %-9s %-7s %s\n" "$skin" "-" "-" "$mae_limit" "FAIL (no render)"
        fail=1
        continue
    fi

    result=$(python3 - "$ref" "$shot" "$mae_limit" <<'PY'
import sys
from PIL import Image
ref_im = Image.open(sys.argv[1]).convert('RGBA')
cur_im = Image.open(sys.argv[2]).convert('RGBA')
limit = float(sys.argv[3])
def trim(im):
    # Cut fully-transparent margins: unpainted layout remainder that a
    # real window region clips away (author screenshots are region-cut).
    bbox = im.getchannel('A').getbbox()
    return im.crop(bbox) if bbox else im
ref = trim(ref_im).convert('RGB')
cur = trim(cur_im).convert('RGB')
# Author screenshots are sometimes 1px larger than the window (capture
# fringe); tolerate that by cropping both to the common size when the
# difference is at most 1px per axis.
dw, dh = abs(ref.width - cur.width), abs(ref.height - cur.height)
if dw > 1 or dh > 1:
    print(f"{cur.width}x{cur.height}\t-\tFAIL (size, ref {ref.width}x{ref.height})")
    sys.exit(0)
w, h = min(ref.width, cur.width), min(ref.height, cur.height)
ref = ref.crop((0, 0, w, h)); cur = cur.crop((0, 0, w, h))
rb, cb = ref.tobytes(), cur.tobytes()
mae = sum(abs(a - b) for a, b in zip(rb, cb)) / len(rb)
verdict = "PASS" if mae <= limit else "FAIL (mae)"
print(f"{cur.width}x{cur.height}\t{mae:.1f}\t{verdict}")
PY
)
    size=$(echo "$result" | cut -f1)
    mae=$(echo "$result" | cut -f2)
    verdict=$(echo "$result" | cut -f3)
    printf "%-18s %-11s %-9s %-7s %s\n" "$skin" "$size" "$mae" "$mae_limit" "$verdict"
    [[ "$verdict" == PASS ]] || fail=1
done < "$MANIFEST"

exit "$fail"
