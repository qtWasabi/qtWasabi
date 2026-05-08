#!/usr/bin/env bash
# tests/visual/sbs_titlebar.sh — render the current Modern main/normal
# layout, crop just the titlebar strip, and stack it below the
# reference WACUP titlebar from /tmp/sbs_13.png so we can eye-check
# spacing/streak/center positioning.
#
# Output: /tmp/sbs_titlebar.png
set -euo pipefail

SKIN="${WASABIQT_TEST_SKIN:-$HOME/.winamp/skins/Winamp Modern/skin.xml}"
THEME="${1:-Blues}"
RENDER="${2:-$(pwd)/build/tests/visual/render_layout}"
REF="${WASABIQT_REF_TITLEBAR:-$(dirname "$0")/expected/wacup_titlebar_reference.png}"

if [ ! -f "$SKIN" ]; then echo "skin missing: $SKIN" >&2; exit 1; fi
if [ ! -x "$RENDER" ]; then echo "render_layout missing: $RENDER" >&2; exit 1; fi

# Render the full layout at 354x280.
QT_QPA_PLATFORM=offscreen "$RENDER" "$SKIN" /tmp/sbs_full.png \
    --theme "$THEME" --container main --layout normal --w 354 --h 280 \
    --display time=00:42 >/dev/null

# Crop the titlebar strip (top 22px) so we can compare against the
# reference titlebar style.
python3 -c "
from PIL import Image
img = Image.open('/tmp/sbs_full.png').convert('RGBA')
strip = img.crop((0, 0, img.size[0], 22))
strip.save('/tmp/sbs_strip.png')
print(strip.size)
"

# Stack reference (top) + ours (bottom) side by side vertically.
python3 -c "
import os, sys
from PIL import Image
ours = Image.open('/tmp/sbs_strip.png').convert('RGBA')
ref_path = '$REF'
if not os.path.exists(ref_path):
    ours.save('/tmp/sbs_titlebar.png')
    print('no reference at', ref_path, '— saved ours-only')
    sys.exit()
ref = Image.open(ref_path).convert('RGBA')
# Resize ref to match our width (preserve aspect)
ref_w = ours.size[0]
ref_h = int(ref.size[1] * (ref_w / ref.size[0]))
ref = ref.resize((ref_w, ref_h), Image.NEAREST)
out = Image.new('RGBA', (ours.size[0], ref.size[1] + ours.size[1] + 4),
                (40, 40, 40, 255))
out.paste(ref, (0, 0))
out.paste(ours, (0, ref.size[1] + 4))
out.save('/tmp/sbs_titlebar.png')
print('wrote /tmp/sbs_titlebar.png  size=', out.size)
"
