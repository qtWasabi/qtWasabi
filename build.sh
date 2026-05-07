#!/usr/bin/env bash
#
# build.sh — one-command build for WasabiQT.
#
# Fetches the Wasabi source from archive.org if not already present
# (./scripts/fetch-wasabi.sh), configures CMake against it, builds,
# and optionally builds an RPM (Fedora) or .app bundle (macOS).
#
# Usage:
#     ./build.sh                   # configure + build, no install
#     ./build.sh install           # configure + build + sudo install
#     ./build.sh rpm               # build an RPM into ./packaging/rpm/RPMS
#     ./build.sh dmg               # build a macOS .dmg into ./packaging/macos
#     ./build.sh clean             # rm -rf build/ wasabi-src/
#

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${REPO_ROOT}"

ACTION="${1:-build}"

case "${ACTION}" in
    clean)
        echo "build.sh: removing build/, wasabi-src/"
        rm -rf build/ wasabi-src/
        exit 0
        ;;
esac

# ── source ─────────────────────────────────────────────────────
if [[ ! -d "wasabi-src/Src/Wasabi" && -z "${WASABI_SRC_DIR:-}" ]]; then
    echo "build.sh: no Wasabi source detected — running fetch-wasabi.sh"
    ./scripts/fetch-wasabi.sh
fi

# ── configure ──────────────────────────────────────────────────
CMAKE_ARGS=(
    -B build
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)
if [[ -n "${WASABI_SRC_DIR:-}" ]]; then
    CMAKE_ARGS+=(-DWASABI_SRC_DIR="${WASABI_SRC_DIR}")
fi

echo "build.sh: cmake configure"
cmake "${CMAKE_ARGS[@]}"

# ── build ──────────────────────────────────────────────────────
JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
echo "build.sh: cmake build -j${JOBS}"
cmake --build build -j"${JOBS}"

case "${ACTION}" in
    install)
        echo "build.sh: sudo cmake --install build"
        sudo cmake --install build
        ;;
    rpm)
        echo "build.sh: building RPM"
        ./packaging/rpm/build-rpm.sh
        ;;
    dmg)
        echo "build.sh: building macOS .dmg"
        ./packaging/macos/build-dmg.sh
        ;;
    test)
        echo "build.sh: running tests"
        ctest --test-dir build --output-on-failure
        ;;
    build)
        echo "build.sh: ✓ done.  binaries in ./build/"
        ;;
    *)
        echo "build.sh: unknown action '${ACTION}'" >&2
        echo "  valid: build (default), install, rpm, dmg, test, clean" >&2
        exit 2
        ;;
esac
