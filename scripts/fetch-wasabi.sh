#!/usr/bin/env bash
#
# fetch-wasabi.sh — download the user-supplied Wasabi source from
# the public archive.org mirror into ./wasabi-src/.
#
# This script is part of WasabiQT's BUILD tooling, not its
# redistribution.  archive.org is the source of truth; this script
# just automates the user's own download for convenience.  The
# downloaded tree lands in ./wasabi-src/ which is .gitignored and
# never committed.
#
# Mirrors:
#   - https://archive.org/details/winamp.7z          (community backup)
#   - https://archive.org/details/winamp-srcarc      (alternate mirror)
#
# Run from the WasabiQT repo root:
#
#     ./scripts/fetch-wasabi.sh
#
# After this, `cmake -B build` will find ./wasabi-src/Src/Wasabi
# and configure successfully.
#

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WASABI_SRC_LOCAL="${REPO_ROOT}/wasabi-src"

# archive.org item identifiers — pick the one that resolves first.
# (The user can override by setting WASABIQT_ARCHIVE_URL.)
DEFAULT_MIRRORS=(
    "https://archive.org/download/winamp.7z/winamp.7z"
    "https://archive.org/download/winamp-srcarc/winamp-src.7z"
)

URL="${WASABIQT_ARCHIVE_URL:-${DEFAULT_MIRRORS[0]}}"

# ── tooling check ──────────────────────────────────────────────
need() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "fetch-wasabi: missing '$1'.  Install it first:" >&2
        echo "  Fedora:  sudo dnf install -y $2" >&2
        echo "  macOS :  brew install $2" >&2
        echo "  Debian:  sudo apt install -y $2" >&2
        exit 1
    }
}
need curl   curl
need 7z     p7zip

# ── fetch ──────────────────────────────────────────────────────
mkdir -p "${WASABI_SRC_LOCAL}"

if [[ -d "${WASABI_SRC_LOCAL}/Src/Wasabi" ]]; then
    echo "fetch-wasabi: ${WASABI_SRC_LOCAL}/Src/Wasabi already exists; skipping download."
    echo "fetch-wasabi: delete ${WASABI_SRC_LOCAL} and re-run to refresh."
    exit 0
fi

ARCHIVE_FILE="${WASABI_SRC_LOCAL}/winamp-src.7z"

echo "fetch-wasabi: downloading from ${URL}"
echo "fetch-wasabi: this is a several-hundred-MB archive; allow time."

curl -L --fail --progress-bar -o "${ARCHIVE_FILE}" "${URL}"

# ── extract ────────────────────────────────────────────────────
echo "fetch-wasabi: extracting (7z, may take a minute)"
( cd "${WASABI_SRC_LOCAL}" && 7z x -y "${ARCHIVE_FILE}" >/dev/null )
rm -f "${ARCHIVE_FILE}"

# Find the Src/ root inside whatever the archive's top-level layout is.
# Most mirrors extract to ./winamp-src/Src/ directly; some include an
# extra wrapper directory.
if [[ ! -d "${WASABI_SRC_LOCAL}/Src/Wasabi" ]]; then
    # search up to 3 levels deep for a Src/Wasabi
    found=$(find "${WASABI_SRC_LOCAL}" -maxdepth 4 -type d -name "Wasabi" 2>/dev/null \
            | xargs -I{} dirname {} | head -1 || true)
    if [[ -n "$found" && -d "$found/Wasabi" ]]; then
        echo "fetch-wasabi: relocating ${found} -> ${WASABI_SRC_LOCAL}/Src"
        mkdir -p "${WASABI_SRC_LOCAL}/Src"
        mv "${found}"/* "${WASABI_SRC_LOCAL}/Src/"
    else
        echo "fetch-wasabi: ERROR — cannot locate Src/Wasabi inside archive" >&2
        echo "fetch-wasabi: tree dump (top 5):" >&2
        find "${WASABI_SRC_LOCAL}" -maxdepth 3 -type d | head -5 >&2
        exit 2
    fi
fi

# ── verify ─────────────────────────────────────────────────────
required=(
    "Src/Wasabi/api/script"
    "Src/Wasabi/api/wnd"
    "Src/Wasabi/api/skin"
    "Src/Wasabi/bfc"
    "Src/Wasabi/Lib"
)
for d in "${required[@]}"; do
    if [[ ! -d "${WASABI_SRC_LOCAL}/${d}" ]]; then
        echo "fetch-wasabi: ERROR — missing required subdir ${d}" >&2
        exit 3
    fi
done

echo "fetch-wasabi: ✓ extracted into ${WASABI_SRC_LOCAL}/Src"
echo "fetch-wasabi: ✓ all expected subdirs present"
echo
echo "Next:  cmake -B build && cmake --build build -j"
