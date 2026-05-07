#!/usr/bin/env bash
#
# build-dmg.sh — build a WasabiQT .app bundle and .dmg on macOS.
#
# Targets Apple Silicon natively (M-series).  Universal binary
# (arm64+x86_64) optional via WASABIQT_UNIVERSAL=1.
#

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${REPO_ROOT}"

[[ "$(uname)" == "Darwin" ]] || {
    echo "build-dmg: macOS only" >&2; exit 1;
}

# Tooling
command -v cmake >/dev/null || { echo "Install cmake: brew install cmake"; exit 1; }
command -v create-dmg >/dev/null || \
    echo "build-dmg: tip — 'brew install create-dmg' for prettier .dmg layout"

# Default to native arch; opt into universal via env.
ARCHS="$(uname -m)"
[[ "${WASABIQT_UNIVERSAL:-0}" == "1" ]] && ARCHS="arm64;x86_64"

# Source
[[ -d wasabi-src/Src/Wasabi ]] || ./scripts/fetch-wasabi.sh

# Configure + build
cmake -B build-mac \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="${ARCHS}" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DWASABIQT_BUILD_TESTS=OFF

cmake --build build-mac -j"$(sysctl -n hw.ncpu)"

# Stage .app bundle
STAGING="packaging/macos/dmg-staging"
APP="${STAGING}/WasabiQT.app"
rm -rf "${STAGING}" && mkdir -p "${APP}/Contents/"{MacOS,Frameworks,Resources}

cp build-mac/examples/minimal_player/wasabiqt_minimal_player \
   "${APP}/Contents/MacOS/WasabiQT"
cp build-mac/src/libwasabiqt*.dylib \
   "${APP}/Contents/Frameworks/" 2>/dev/null || true

cat > "${APP}/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
   "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>             <string>WasabiQT</string>
    <key>CFBundleExecutable</key>       <string>WasabiQT</string>
    <key>CFBundleIdentifier</key>       <string>at.snek.WasabiQT</string>
    <key>CFBundleVersion</key>          <string>0.0.1</string>
    <key>CFBundleShortVersionString</key><string>0.0.1</string>
    <key>LSMinimumSystemVersion</key>   <string>11.0</string>
    <key>NSHighResolutionCapable</key>  <true/>
</dict>
</plist>
EOF

# Bundle Qt frameworks via macdeployqt
QTBIN="$(brew --prefix qt 2>/dev/null)/bin"
[[ -x "${QTBIN}/macdeployqt" ]] && "${QTBIN}/macdeployqt" "${APP}"

# Make the .dmg
DMG="packaging/macos/WasabiQT-0.0.1-$(uname -m).dmg"
rm -f "${DMG}"
if command -v create-dmg >/dev/null; then
    create-dmg --volname "WasabiQT" --window-size 480 320 \
               --icon-size 100 --icon "WasabiQT.app" 120 160 \
               --app-drop-link 360 160 \
               "${DMG}" "${APP}"
else
    hdiutil create -volname "WasabiQT" -srcfolder "${APP}" \
                   -ov -format UDZO "${DMG}"
fi

echo
echo "build-dmg: ✓ ${DMG}"
