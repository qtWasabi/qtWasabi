cmake_minimum_required(VERSION 3.21)
project(WasabiQT
    VERSION 0.0.1
    DESCRIPTION "Qt-native Wasabi/Maki skin engine"
    HOMEPAGE_URL "https://github.com/kleberbaum/WasabiQT"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Position-independent code by default; allows building as a shared library
# without re-jiggering individual targets.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Qt6 (Core, Gui, Widgets, Multimedia for vis hooks).
# WasabiQT targets the LATEST Qt6 — no fallback to 6.0/6.1 era.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Locate the user-supplied Wasabi source tree.  We do NOT redistribute
# any of it — the user obtains it themselves (e.g. from the
# archive.org mirror) and points us at it via -DWASABI_SRC_DIR=...
# or the WASABI_SRC_DIR environment variable.  scripts/fetch-wasabi.sh
# automates the download into ./wasabi-src/Src for local builds.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindWasabiSrc.cmake)

# ── Targets ─────────────────────────────────────────────────────────

# qt6/ — Qt rendering + event adapter.  Modern-Qt re-derivation of
# the 2015-era /Src/Wasabi/qt6/ adapter shim.  Used by src/.
add_subdirectory(qt6)

# wasabi-port/ — minimum BFC subset from WASABI_SRC_DIR, compiled
# against wasabi-port-shim.h to bridge the unported Linux/macOS layers.
# Exposes WASABIQT_PORT_OBJECTS / _SHIM / _INCLUDES to the parent scope.
add_subdirectory(wasabi-port)

# src/ — engine integration glue (host bridge, skin loader bootstrap).
# Compiles against the user's Wasabi source via WASABI_SRC_DIR.
add_subdirectory(src)

# Tests — pixel-regression harness against canonical reference renders.
option(WASABIQT_BUILD_TESTS "Build WasabiQT regression tests" ON)
if(WASABIQT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples — minimal skin viewer that opens a layout in a Qt window.
option(WASABIQT_BUILD_EXAMPLES "Build WasabiQT example apps" ON)
if(WASABIQT_BUILD_EXAMPLES)
    add_subdirectory(examples/wq_view)
endif()

# Reference embedder: github.com/kleberbaum/winamp-linux  (separate
# repo, links against this lib via find_package(WasabiQT)).  No
# in-tree example player — winamp-linux IS the example.

# ── Install ─────────────────────────────────────────────────────────

include(GNUInstallDirs)
install(DIRECTORY public/WasabiQt
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h")

message(STATUS "WasabiQT ${PROJECT_VERSION} configured")
message(STATUS "  Qt6:           ${Qt6_VERSION}")
message(STATUS "  WASABI_SRC_DIR: ${WASABI_SRC_DIR}")
message(STATUS "  C++ standard:  ${CMAKE_CXX_STANDARD}")
