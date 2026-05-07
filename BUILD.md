# Building WasabiQT

This document covers everything needed to build, install, and package
WasabiQT from source.  TL;DR for the impatient:

```bash
curl -fsSL https://wasabiqt.snek.at | sh
```

That installs from scratch on Fedora, RHEL, Debian, Ubuntu, Arch,
openSUSE, and macOS (Apple Silicon native + Intel).  Everything below
is what's happening under the hood, in case you want to do it
manually or you're packaging WasabiQT for a distro.

---

## What you'll get

- `libwasabiqt.so` (or `.dylib`, `.dll`) — the embeddable skin engine
- `WasabiQt::Host` / `WasabiQt::Skin` headers under `include/WasabiQt/`
- `wasabiqt_minimal_player` — example app, hosts a `.wal` skin in a
  QMainWindow

## What you'll need

- **Qt 6.6 or newer** (Core, Gui, Widgets, Multimedia)
- **C++20 compiler** — gcc 12+, clang 15+, MSVC 2022+, AppleClang 14+
- **CMake 3.21 or newer**
- **curl + 7z** (for fetching the user-supplied Wasabi source)
- **git**
- ~500 MB free disk for the Wasabi source extract + ~200 MB for the build

## Why WasabiQT needs you to fetch the Wasabi source separately

The Wasabi source — the original Maki VM, widget classes, and BFC
foundation framework that WasabiQT links against — is licensed under
the **Winamp Collaborative License v1.0**, which forbids redistribution
by anyone but Llama Group themselves.  WasabiQT therefore ships
**none** of it.

The source is publicly mirrored on the Internet Archive.  Anyone can
download it themselves; under WCL §3 they then have the right to "make,
run, and propagate Covered works that they do not Convey."  WasabiQT's
build expects you to do that download once, points at your local copy,
and links against it.  No conveyance happens via WasabiQT; it happens
between archive.org and you.

This is the same pattern console emulators use for proprietary BIOSes:
the emulator is freely distributable, the BIOS isn't, you supply the
BIOS yourself.

## Step-by-step manual build

### 1. Install build dependencies

#### Fedora / RHEL / Rocky / Alma
```bash
sudo dnf install -y cmake gcc-c++ git curl p7zip p7zip-plugins \
    qt6-qtbase-devel qt6-qtmultimedia-devel ninja-build pkgconfig
```

#### Debian / Ubuntu / Mint / Pop!_OS
```bash
sudo apt update
sudo apt install -y cmake g++ git curl p7zip-full \
    qt6-base-dev qt6-multimedia-dev ninja-build pkg-config
```

#### Arch / Manjaro / EndeavourOS
```bash
sudo pacman -S --needed cmake gcc git curl p7zip \
    qt6-base qt6-multimedia ninja pkgconf
```

#### openSUSE / SLES
```bash
sudo zypper install -y cmake gcc-c++ git curl p7zip \
    qt6-base-devel qt6-multimedia-devel ninja
```

#### macOS (Homebrew)
```bash
brew install cmake git curl p7zip qt ninja create-dmg
```

### 2. Clone WasabiQT

```bash
git clone https://github.com/kleberbaum/WasabiQT.git
cd WasabiQT
```

### 3. Fetch the Wasabi source from archive.org

```bash
./scripts/fetch-wasabi.sh
```

This downloads the source archive (a few hundred MB) and extracts it
into `./wasabi-src/Src/`.  That directory is gitignored and never
committed.

If you already have a Wasabi source tree somewhere else, skip the
fetch and point CMake at it directly:

```bash
export WASABI_SRC_DIR=/your/path/to/Src
```

The `./scripts/fetch-wasabi.sh` honours `WASABIQT_ARCHIVE_URL` if
you want a different mirror:

```bash
WASABIQT_ARCHIVE_URL="https://archive.org/download/winamp.7z/winamp.7z" \
    ./scripts/fetch-wasabi.sh
```

### 4. Configure

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

Or with explicit source path:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DWASABI_SRC_DIR=/your/path/to/Src
```

CMake will print the located Wasabi source path and Qt version:

```
-- Located Wasabi source at: /home/you/WasabiQT/wasabi-src/Src
-- WasabiQT 0.0.1 configured
--   Qt6:           6.7.2
--   WASABI_SRC_DIR: /home/you/WasabiQT/wasabi-src/Src
--   C++ standard:  20
```

### 5. Build

```bash
cmake --build build -j
```

Results: `build/src/libwasabiqt.so`, `build/examples/minimal_player/wasabiqt_minimal_player`.

### 6. (Optional) Install system-wide

```bash
sudo cmake --install build
```

Default prefix: `/usr/local`.  Override with `-DCMAKE_INSTALL_PREFIX=…`
at configure time.

---

## Packaging

### Build an RPM (Fedora / RHEL / openSUSE)

```bash
./packaging/rpm/build-rpm.sh
```

Output lands in `./packaging/rpm/RPMS/<arch>/`:

- `wasabiqt-<ver>.<arch>.rpm` — the runtime shared library + example
- `wasabiqt-devel-<ver>.<arch>.rpm` — headers + CMake config

Install:
```bash
sudo dnf install ./packaging/rpm/RPMS/$(uname -m)/wasabiqt-*.rpm
```

The RPM spec at `packaging/rpm/wasabiqt.spec` runs
`scripts/fetch-wasabi.sh` from `%prep` so the build host downloads
the Wasabi source fresh on every RPM build.  Nothing Wasabi-licensed
ever lands in the RPM.

### Build a macOS .dmg

```bash
./packaging/macos/build-dmg.sh
```

Output: `./packaging/macos/WasabiQT-<ver>-<arch>.dmg`.

Native build for the host architecture by default.  Universal binary
(arm64+x86_64) via:

```bash
WASABIQT_UNIVERSAL=1 ./packaging/macos/build-dmg.sh
```

The `.app` bundles Qt frameworks via `macdeployqt` so it runs without
a separately-installed Qt.  Targets macOS 11+ (Big Sur and later, all
Apple Silicon Macs).

### One-line installer

The `packaging/installer.sh` script is what `curl https://wasabiqt.snek.at | sh`
runs.  It detects the platform, installs deps, clones, fetches Wasabi
source, builds, and installs system-wide.

Test locally:

```bash
WASABIQT_DRY_RUN=1 ./packaging/installer.sh
```

prints what it *would* do without executing.  Useful for
audit-before-pipe-to-shell people (i.e. the right kind of careful).

Override knobs:

| Variable | Default | What it does |
|---|---|---|
| `WASABIQT_REPO`     | `https://github.com/kleberbaum/WasabiQT.git` | git remote |
| `WASABIQT_REF`      | `main`                | branch / tag / sha |
| `WASABIQT_PREFIX`   | `/usr/local`          | install prefix |
| `WASABIQT_WORK_DIR` | `~/.cache/wasabiqt-build` | where to clone & build |
| `WASABIQT_DRY_RUN`  | `0`                   | print, don't execute |

---

## Embedding in your Qt media player or plugin

WasabiQT ships **two link variants** so each embedder picks whichever
fits their distribution model:

| Variant | Target | Output | When to use |
|---|---|---|---|
| Shared | `WasabiQT::WasabiQT` | runtime dep on `libwasabiqt.so` | system-wide install via RPM/dpkg/brew |
| Static | `WasabiQT::Static`  | bundled into your binary | plugins, Flatpak/Snap/AppImage, single-file builds |

### Path 1 — system-installed shared library (winamp-linux, distro Audacious plugins)

Best when your app is itself distributed via the system package
manager.  WasabiQT installs `libwasabiqt.so`, headers, the CMake
config, and pkg-config; consumers find it the standard way:

```cmake
find_package(WasabiQT 0.0.1 REQUIRED)
target_link_libraries(my_player PRIVATE WasabiQT::WasabiQT)
```

Or via pkg-config (autotools / non-CMake builds):

```bash
gcc $(pkg-config --cflags wasabiqt) -o my_app my_app.cpp \
    $(pkg-config --libs wasabiqt)
```

In your RPM spec / dpkg control file:

```
# Fedora / RHEL
Requires:       wasabiqt
BuildRequires:  wasabiqt-devel
```

```
# Debian / Ubuntu (when packaged that way)
Depends:        libwasabiqt0
Build-Depends:  libwasabiqt-dev
```

`dnf install winamp-linux` then automatically pulls libwasabiqt.so as a
dependency.  This is what your existing Cloudflare-R2 + Fedora repo
should do for winamp-linux.

### Path 2 — bundled static archive (Audacious plugins, Flatpak, AppImage)

Best when your app must ship as a single self-contained artefact —
Audacious plugins distributed independently, Flatpak/Snap bundles,
AppImage builds, single-file Qt media players.

```cmake
find_package(WasabiQT 0.0.1 REQUIRED)
target_link_libraries(my_audacious_plugin PRIVATE WasabiQT::Static)
```

Your plugin .so or app binary then contains all of WasabiQT inline
with no runtime dependency on libwasabiqt.so being present on the
end-user's system.  The user installs Audacious normally, drops your
plugin file into `/usr/lib/audacious/Visualization/` (or wherever),
and it works without any WasabiQT package.

For Audacious plugins specifically:

```cmake
# CMakeLists.txt for an Audacious skin plugin using WasabiQT
cmake_minimum_required(VERSION 3.21)
project(audacious_wasabi_plugin LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(WasabiQT REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(AUDACIOUS REQUIRED audacious)

add_library(audacious_wasabi MODULE plugin.cpp)
target_link_libraries(audacious_wasabi PRIVATE
    WasabiQT::Static                  # bundled, no runtime dep
    Qt6::Core Qt6::Gui Qt6::Widgets
    ${AUDACIOUS_LIBRARIES})

set_target_properties(audacious_wasabi PROPERTIES
    PREFIX ""                          # Audacious expects no "lib" prefix
    LIBRARY_OUTPUT_NAME wasabi)

install(TARGETS audacious_wasabi
    LIBRARY DESTINATION ${AUDACIOUS_PLUGIN_DIR}/General)
```

The plugin's `wasabi.so` is fully self-contained.  You don't depend
on the user having `wasabiqt-devel` or even WasabiQT installed.

### Embedder code (same for both link variants)

```cpp
#include <WasabiQt/Host.h>
#include <WasabiQt/Skin.h>

class MyPlayerHost : public WasabiQt::Host {
    // …implement ~40 virtual methods routing to your playback engine…
};

MyPlayerHost host;
WasabiQt::Skin skin(&host);
skin.load("/path/to/winamp_modern.wal");
mainWindow->setCentralWidget(skin.widget());
```

### Reference embedder

The canonical example of a real player using WasabiQT is
**[github.com/kleberbaum/winamp-linux](https://github.com/kleberbaum/winamp-linux)** —
a Qt6 Winamp clone targeting Linux/macOS/Windows.  Its
`src/winampwasabihost.{h,cpp}` is a fully-implemented
`WasabiQt::Host`, and its CMake build does
`find_package(WasabiQT REQUIRED)` + links `WasabiQT::WasabiQT`.

That's the integration test for WasabiQT itself: if winamp-linux
loads a Winamp Modern skin correctly, WasabiQT works.  No
maintained in-tree stub player — adding one would diverge from
the only real consumer.

### Which path for your specific use cases

- **winamp-linux** (Fedora RPM via Cloudflare R2):
  shared library + `Requires: wasabiqt` in your spec.  Standard.
- **Audacious plugin in Fedora repos**:
  shared library + `Requires: wasabiqt`.  Distro-packaged plugins
  expect distro-packaged deps.
- **Audacious plugin distributed independently** (your own download
  site, sideloaded into stock Audacious): static.  No assumptions
  about what's on the user's system.
- **Flatpak / Snap / AppImage** of your player or a plugin:
  static.  These bundles are sandboxed; reaching the system's
  libwasabiqt.so isn't permitted.
- **macOS .app bundle**: typically static, or shared with
  `macdeployqt`-bundled Frameworks.  Either works; static is
  simpler.

## Troubleshooting

### `WASABI_SRC_DIR` is missing required subdirectory

The archive.org download was incomplete or you pointed at the wrong
path.  Wipe and refetch:

```bash
rm -rf wasabi-src/
./scripts/fetch-wasabi.sh
```

### Qt version too old

WasabiQT requires Qt 6.6+.  On older distros, install Qt from the Qt
Online Installer or use a vendored Qt.  Then:

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/qt6.6+/install
```

### Build runs out of memory

Common on small VMs / aarch64 build farms because Qt MOC + heavy C++.
Limit parallelism:

```bash
cmake --build build -j2
```

### macOS: "WasabiQT.app is damaged"

This means the app isn't signed (we don't sign by default).  On
Apple Silicon:

```bash
xattr -dr com.apple.quarantine /Applications/WasabiQT.app
```

Or build it yourself (the build above), where Gatekeeper trusts the
locally-built binary.

---

## License

WasabiQT itself: MIT (see `LICENSE`).

The Wasabi source you supply: Winamp Collaborative License v1.0, see
the source archive's own `LICENSE.md`.  Your responsibility to honour.
