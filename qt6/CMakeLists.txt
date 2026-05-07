# qt6/ — modern Qt6 rendering and event adapter for Wasabi.
#
# Re-derivation (not a port) of the 2015-era Src/Wasabi/qt6/ adapter
# stub.  Targets current Qt6 with native event paths, HiDPI, Wayland,
# and no Win32 typedef pretence in the embedder-facing API.

add_library(wasabiqt_qt6 STATIC
    QtCanvasAdapter.cpp
    QtWindowAdapter.cpp
    QtTimerAdapter.cpp
    Win32Shim.cpp
)

target_include_directories(wasabiqt_qt6
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${WASABI_SRC_DIR}
        ${WASABI_SRC_DIR}/Wasabi)

target_link_libraries(wasabiqt_qt6
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets)

target_compile_features(wasabiqt_qt6 PUBLIC cxx_std_20)
