#!/usr/bin/env bash
# tests/visual/compare_visual.sh — render every case described in
# `cases.txt`, write the PNG to `actual/`, and pixel-diff against
# `expected/`.  Exit nonzero on any mismatch.
#
#   compare_visual.sh <render_layout-binary> <visual-test-dir>
#
# Each line of cases.txt is:
#
#   NAME  THEME  CONTAINER  LAYOUT  W  H  EXTRA-ARGS...
#
# `EXTRA-ARGS` are forwarded to `render_layout` verbatim (e.g.
# `--display time=00:42`).  Use `#` for comments, blank lines OK.
#
# Set `WASABIQT_REGEN_GOLDENS=1` to write fresh expected/*.png from
# the current renders (use after a deliberate rendering change).
#
# Skin path defaults to ~/.winamp/skins/Winamp Modern; override via
# `WASABIQT_TEST_SKIN`.

set -u
RENDER="$1"
DIR="$2"

SKIN="${WASABIQT_TEST_SKIN:-$HOME/.winamp/skins/Winamp Modern/skin.xml}"
if [ ! -f "$SKIN" ]; then
    echo "skip: skin not installed at $SKIN" >&2
    exit 0
fi

mkdir -p "$DIR/actual"

cases="$DIR/cases.txt"
if [ ! -f "$cases" ]; then
    echo "skip: no cases.txt" >&2
    exit 0
fi

failed=0
total=0
while IFS= read -r line; do
    case "$line" in
        ''|'#'*) continue ;;
    esac

    # Split on '|' — supports spaces in theme names.  Trim whitespace
    # via a sed substitution rather than xargs (which mangles the
    # single quote in "Good Ol' Winamp").
    trim() { sed -E 's/^[[:space:]]+//;s/[[:space:]]+$//' <<<"$1"; }

    IFS='|' read -r raw_name raw_theme raw_container raw_layout raw_w raw_h raw_extra <<<"$line"
    name=$(trim "$raw_name")
    theme=$(trim "$raw_theme")
    container=$(trim "$raw_container")
    layout_id=$(trim "$raw_layout")
    w=$(trim "$raw_w")
    h=$(trim "$raw_h")
    extra=$(trim "$raw_extra")

    actual="$DIR/actual/$name.png"
    expected="$DIR/expected/$name.png"

    if ! "$RENDER" "$SKIN" "$actual" \
            --theme "$theme" \
            --container "$container" \
            --layout    "$layout_id" \
            --w "$w" --h "$h" \
            $extra >/dev/null; then
        echo "FAIL: render $name failed"
        failed=$((failed+1))
        total=$((total+1))
        continue
    fi
    total=$((total+1))

    if [ "${WASABIQT_REGEN_GOLDENS:-0}" = "1" ] || [ ! -f "$expected" ]; then
        cp "$actual" "$expected"
        echo "wrote golden: $name"
        continue
    fi

    diff_pct=$(python3 -c "
from PIL import Image
import numpy as np, sys
a = np.array(Image.open('$actual').convert('RGBA'))
b = np.array(Image.open('$expected').convert('RGBA'))
if a.shape != b.shape:
    print('100.0'); sys.exit(0)
mismatches = (a != b).any(axis=-1).sum()
print(f'{100.0 * mismatches / (a.shape[0] * a.shape[1]):.3f}')
")

    threshold="0.500"
    if python3 -c "import sys; sys.exit(0 if float('$diff_pct') > float('$threshold') else 1)"; then
        echo "FAIL: $name — $diff_pct% pixels differ (>${threshold}%)"
        failed=$((failed+1))
    else
        echo "PASS: $name — $diff_pct% pixels differ"
    fi
done < "$cases"

echo "----"
echo "$((total - failed)) / $total passed"
exit $failed
