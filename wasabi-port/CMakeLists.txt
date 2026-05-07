# wasabi-port/ — minimum BFC subset compiled with the port-shim.
#
# WasabiQT's hybrid approach: vendor the opensourced Maki VM source bit-perfect
# from the user's WASABI_SRC_DIR (NEVER from this repo's tree), build it
# against a small force-included header that papers over the unported
# Linux/macOS layers (`std_file`, `std_keyboard`, `std_wnd`, locale).
#
# Only the sources listed below are needed.  Notable exclusions:
#   - `wasabi_std.cpp` — has X11 mouse / Win32 timer paths and is unused
#     by the Maki VM (verified: vcpu/scriptmgr/objecttable reference 0
#     symbols from it).  Replace with a Qt-native equivalent later.
#   - `critsec.cpp`   — Linux branch references undeclared `cs.mutex`;
#     VM doesn't use CriticalSection at all (verified the same way), so
#     skip rather than patch.
#   - `thread.cpp`    — same story as critsec.

set(WASABIQT_PORT_SHIM   ${CMAKE_CURRENT_SOURCE_DIR}/wasabi-port-shim.h)

set(WASABIQT_BFC_SOURCES
    ${WASABI_SRC_DIR}/Wasabi/bfc/memblock.cpp
    ${WASABI_SRC_DIR}/Wasabi/bfc/foreach.cpp
    ${WASABI_SRC_DIR}/Wasabi/bfc/ptrlist.cpp
    ${WASABI_SRC_DIR}/Wasabi/bfc/stack.cpp
    ${WASABI_SRC_DIR}/Wasabi/bfc/node.cpp
    ${WASABI_SRC_DIR}/Wasabi/bfc/freelist.cpp
    ${WASABI_SRC_DIR}/Wasabi/bfc/wasabi_std_rect.cpp
    ${WASABI_SRC_DIR}/Wasabi/bfc/nsguid.cpp

    # Linux/Qt implementations of the platform hooks BFC declares but
    # only provides on Win32 / macOS.  Lives in this repo (legally clean).
    ${CMAKE_CURRENT_SOURCE_DIR}/wasabi-port-stubs.cpp
)

# Object library — the .o files are pulled into both the shared and the
# static libwasabiqt without rebuilding twice.  Downstream targets in
# src/ refer to it via the WASABIQT_PORT_OBJECTS variable below.
add_library(wasabiqt_port OBJECT ${WASABIQT_BFC_SOURCES})

set(WASABIQT_PORT_INCLUDES
    ${WASABI_SRC_DIR}
    ${WASABI_SRC_DIR}/Wasabi
    ${WASABI_SRC_DIR}/Wasabi/bfc
    CACHE INTERNAL "")

target_include_directories(wasabiqt_port PRIVATE ${WASABIQT_PORT_INCLUDES})

target_compile_options(wasabiqt_port PRIVATE
    -include ${WASABIQT_PORT_SHIM}
    -DLINUX
    -Wno-register
    -Wno-overflow
    -Wno-cpp
    -Wno-write-strings
    -Wno-narrowing)

set_target_properties(wasabiqt_port PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

# Expose to parent scope (consumed by src/CMakeLists.txt).
set(WASABIQT_PORT_OBJECTS  $<TARGET_OBJECTS:wasabiqt_port>  PARENT_SCOPE)
set(WASABIQT_PORT_SHIM     ${WASABIQT_PORT_SHIM}            PARENT_SCOPE)
set(WASABIQT_PORT_INCLUDES ${WASABIQT_PORT_INCLUDES}        PARENT_SCOPE)
