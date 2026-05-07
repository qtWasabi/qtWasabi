#!/usr/bin/env bash
#
# WasabiQT installer — `curl https://wasabiqt.snek.at | sh`-style.
#
# Detects host OS, installs Qt6 + build tools via the native package
# manager, clones WasabiQT, fetches the Wasabi source from
# archive.org, builds, and installs.
#
# Modeled on Asahi's alx.sh: minimal output, asks for sudo only when
# it actually needs root, fails loudly with an actionable message
# otherwise.
#
# DRY-RUN: WASABIQT_DRY_RUN=1 only prints commands.
#

set -euo pipefail

WASABIQT_REPO="${WASABIQT_REPO:-https://github.com/kleberbaum/WasabiQT.git}"
WASABIQT_REF="${WASABIQT_REF:-main}"
INSTALL_PREFIX="${WASABIQT_PREFIX:-/usr/local}"
WORK_DIR="${WASABIQT_WORK_DIR:-${HOME}/.cache/wasabiqt-build}"

run() {
    if [[ "${WASABIQT_DRY_RUN:-0}" == "1" ]]; then
        echo "+ $*"
    else
        "$@"
    fi
}

say()   { printf '\033[1;36m::\033[0m %s\n' "$*"; }
warn()  { printf '\033[1;33m!!\033[0m %s\n' "$*" >&2; }
die()   { printf '\033[1;31m✗\033[0m %s\n' "$*" >&2; exit 1; }

# ── platform detection ────────────────────────────────────────
if [[ "$(uname)" == "Darwin" ]]; then
    PLATFORM="macos"
elif [[ -f /etc/os-release ]]; then
    . /etc/os-release
    case "${ID,,}" in
        fedora|rhel|centos|rocky|almalinux) PLATFORM="fedora" ;;
        debian|ubuntu|linuxmint|pop)        PLATFORM="debian" ;;
        arch|manjaro|endeavouros)           PLATFORM="arch"   ;;
        opensuse*|sles)                     PLATFORM="suse"   ;;
        *) PLATFORM="unknown" ;;
    esac
else
    PLATFORM="unknown"
fi

say "WasabiQT installer"
say "Platform:        ${PLATFORM}"
say "Install prefix:  ${INSTALL_PREFIX}"
say "Build dir:       ${WORK_DIR}"
echo

# ── deps ──────────────────────────────────────────────────────
case "${PLATFORM}" in
    fedora)
        say "Installing build deps via dnf"
        run sudo dnf install -y \
            cmake gcc-c++ git curl p7zip p7zip-plugins \
            qt6-qtbase-devel qt6-qtmultimedia-devel \
            ninja-build pkgconfig
        ;;
    debian)
        say "Installing build deps via apt"
        run sudo apt update
        run sudo apt install -y \
            cmake g++ git curl p7zip-full \
            qt6-base-dev qt6-multimedia-dev \
            ninja-build pkg-config
        ;;
    arch)
        say "Installing build deps via pacman"
        run sudo pacman -S --needed --noconfirm \
            cmake gcc git curl p7zip \
            qt6-base qt6-multimedia ninja pkgconf
        ;;
    suse)
        say "Installing build deps via zypper"
        run sudo zypper install -y \
            cmake gcc-c++ git curl p7zip \
            qt6-base-devel qt6-multimedia-devel ninja
        ;;
    macos)
        command -v brew >/dev/null \
            || die "Homebrew required.  Install: https://brew.sh"
        say "Installing build deps via brew"
        run brew install cmake git curl p7zip qt ninja create-dmg
        ;;
    *)
        die "Unsupported platform.  Install manually:
            cmake >= 3.21, gcc/clang, git, curl, p7zip,
            Qt6 (Core, Gui, Widgets, Multimedia), ninja, pkg-config."
        ;;
esac

# ── clone ─────────────────────────────────────────────────────
mkdir -p "$(dirname "${WORK_DIR}")"

if [[ -d "${WORK_DIR}/.git" ]]; then
    say "Updating ${WORK_DIR}"
    run git -C "${WORK_DIR}" fetch origin
    run git -C "${WORK_DIR}" checkout "${WASABIQT_REF}"
    run git -C "${WORK_DIR}" pull --ff-only
else
    say "Cloning ${WASABIQT_REPO}"
    run git clone --branch "${WASABIQT_REF}" --depth 1 \
        "${WASABIQT_REPO}" "${WORK_DIR}"
fi

# ── fetch Wasabi source ───────────────────────────────────────
say "Fetching Wasabi source from archive.org"
run "${WORK_DIR}/scripts/fetch-wasabi.sh"

# ── build ─────────────────────────────────────────────────────
say "Configuring + building (Release)"
run cmake -S "${WORK_DIR}" -B "${WORK_DIR}/build" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DWASABIQT_BUILD_TESTS=OFF \
    -DWASABIQT_BUILD_EXAMPLES=ON

run cmake --build "${WORK_DIR}/build" \
    -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# ── install ───────────────────────────────────────────────────
say "Installing into ${INSTALL_PREFIX}"
if [[ -w "${INSTALL_PREFIX}" ]]; then
    run cmake --install "${WORK_DIR}/build"
else
    run sudo cmake --install "${WORK_DIR}/build"
fi

echo
say "✓ WasabiQT installed."
say "  Try:  ${INSTALL_PREFIX}/bin/wasabiqt_minimal_player /path/to/your.wal"
say "  Or embed libwasabiqt in your own Qt media player."
