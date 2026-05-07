# qt6/ — modern Qt6 rendering and event adapter for Wasabi.
#
# Re-derivation (not a port) of the 2015-era Src/Wasabi/qt6/ adapter
# stub.  Targets current Qt6 with native event paths, HiDPI, Wayland,
# and no Win32 typedef pretence in the embedder-facing API.
#
# Sources are compiled directly into wasabiqt / wasabiqt_static (see
# src/CMakeLists.txt) rather than as a separate library — keeps the
# install/export surface minimal.

set(WASABIQT_QT6_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/QtCanvasAdapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/QtWindowAdapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/QtTimerAdapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Win32Shim.cpp
    PARENT_SCOPE)

set(WASABIQT_QT6_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR} PARENT_SCOPE)
