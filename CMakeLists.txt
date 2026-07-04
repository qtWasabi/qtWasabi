cmake_minimum_required(VERSION 3.21)
project(qtWasabi
    VERSION 0.0.1
    DESCRIPTION "Qt-native Wasabi/Maki skin engine"
    HOMEPAGE_URL "https://github.com/kleberbaum/qtWasabi"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Position-independent code by default; allows building as a shared library
# without re-jiggering individual targets.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# macOS: several ported targets fake _MSC_VER so upstream Win32 source
# compiles.  clang's own <stddef.h> then follows MSVC's wchar_t codepath
# and re-typedefs the wchar_t keyword ("cannot combine with previous
# 'int'").  Declaring wchar_t native (which it is in C++) suppresses that
# typedef.  A no-op for TUs that don't fake _MSC_VER, so define it globally.
if(APPLE)
    add_compile_definitions(_NATIVE_WCHAR_T_DEFINED=1)
endif()

# Qt6 (Core, Gui, Widgets, Multimedia for vis hooks).
# qtWasabi targets the LATEST Qt6 — no fallback to 6.0/6.1 era.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Widgets Quick Qml)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Locate the user-supplied Wasabi source tree.  We do NOT redistribute
# any of it — the user obtains it themselves (e.g. from the
# archive.org mirror) and points us at it via -DWASABI_SRC_DIR=...
# or the WASABI_SRC_DIR environment variable.  scripts/fetch-wasabi.sh
# automates the download into ./wasabi-src/Src for local builds.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindWasabiSrc.cmake)

# Overlay the port's own headers that public Wasabi source mirrors don't
# ship — the macOS platform types (macOS was never an upstream build target)
# and a forward-decl stub for a Windows-only header Winamp's Main.h pulls in.
# These are `../`-relative or platform-dispatch includes, so they must sit
# physically inside the source tree.  Only copy when actually absent, so a
# pre-populated, read-only, or shared WASABI_SRC_DIR isn't mutated on every
# configure (and an existing hand-placed stub isn't clobbered).
if(NOT EXISTS "${WASABI_SRC_DIR}/Elevator/IFileTypeRegistrar_32.h"
   OR (APPLE AND NOT EXISTS
       "${WASABI_SRC_DIR}/replicant/foundation/osx-amd64/types.h")
   OR (EMSCRIPTEN AND NOT EXISTS
       "${WASABI_SRC_DIR}/replicant/foundation/wasm32/types.h"))
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/wasabi-port/platform-overlay/Src/
         DESTINATION ${WASABI_SRC_DIR})
endif()

# ── Targets ─────────────────────────────────────────────────────────

# qt6/ — Qt rendering + event adapter.  Modern-Qt re-derivation of
# the 2015-era /Src/Wasabi/qt6/ adapter shim.  Used by src/.
add_subdirectory(qt6)

# wasabi-port/ — minimum BFC subset from WASABI_SRC_DIR, compiled
# against wasabi-port-shim.h to bridge the unported Linux/macOS layers.
# Exposes QTWASABI_PORT_OBJECTS / _SHIM / _INCLUDES to the parent scope.
add_subdirectory(wasabi-port)

# wasabi-compat/ — Win32 + Wasabi-API compatibility layer for porting
# gen_ml / ml_* plugins.  Sibling to wasabi-port/ at a different layer:
# the BFC port papers over BFC's unported Linux hooks, while this one
# provides Win32 typedefs + Wasabi service implementations so a real
# ml plugin compiles unmodified against Qt6 instead of Win32 + Wasabi.
# Exposes QTWASABI_COMPAT_OBJECTS / _SHIM / _INCLUDES.
add_subdirectory(wasabi-compat)

# src/ — engine integration glue (host bridge, skin loader bootstrap).
# Compiles against the user's Wasabi source via WASABI_SRC_DIR.
# Builds the `qtwasabi` SHARED library that plugins link against.
add_subdirectory(src)

# plugins/ — ported gen_ml + ml_* plugin .so modules.  Each is loaded
# at runtime via dlopen by `loadBuiltinMlPlugins()` in src/ml/.
# Plugins link against libqtwasabi for the wasabi-compat layer's
# SendMessageW / ImageList_* / handle-registry / etc., so this MUST
# come after `src/` so the qtwasabi target exists to link against.
option(QTWASABI_BUILD_PLUGINS "Build ported gen_ml + ml_* plugins" ON)
if(QTWASABI_BUILD_PLUGINS)
    add_subdirectory(plugins)
endif()

# Tests — pixel-regression harness against canonical reference renders.
option(QTWASABI_BUILD_TESTS "Build qtWasabi regression tests" ON)
if(QTWASABI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(tests/visual)
endif()

# Examples — minimal skin viewer that opens a layout in a Qt window.
option(QTWASABI_BUILD_EXAMPLES "Build qtWasabi example apps" ON)
if(QTWASABI_BUILD_EXAMPLES)
    add_subdirectory(examples/wq_view)
endif()

# Reference embedder: github.com/kleberbaum/winamp-linux  (separate
# repo, links against this lib via find_package(qtWasabi)).  No
# in-tree example player — winamp-linux IS the example.

# ── Install ─────────────────────────────────────────────────────────

include(GNUInstallDirs)
install(DIRECTORY public/qtWasabi
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h")

message(STATUS "qtWasabi ${PROJECT_VERSION} configured")
message(STATUS "  Qt6:           ${Qt6_VERSION}")
message(STATUS "  WASABI_SRC_DIR: ${WASABI_SRC_DIR}")
message(STATUS "  C++ standard:  ${CMAKE_CXX_STANDARD}")
