# wasabi-port/ — minimum BFC subset + opensourced Maki VM compiled with the
# port-shim.
#
# WasabiQT's hybrid approach: vendor the opensourced Maki VM source bit-perfect
# from the user's WASABI_SRC_DIR (NEVER from this repo's tree), build it
# against a small force-included header that papers over the unported
# Linux/macOS layers (`std_file`, `std_keyboard`, `std_wnd`, locale)
# AND a tiny `include-stubs/` overlay that intercepts a handful of
# windowing-adjacent headers (`tataki/canvas/Canvas.h`,
# `api/timer/api_timer.h`) which the VM only needs for declarations.
#
# vcpu.cpp lives in `patches/` form rather than direct vendoring: the
# upstream copy in WASABI_SRC_DIR is copied into the build tree and a
# qtWasabi-owned patch is applied on top.  Keeps the source-of-truth
# upstream untouched while letting qtWasabi carry the small mutations
# the dispatcher needs (object-shaped receiver acceptance, opcode/
# CALLM tracing, configDummy fallback).

set(WASABIQT_PORT_SHIM    ${CMAKE_CURRENT_SOURCE_DIR}/wasabi-port-shim.h)
set(WASABIQT_PORT_STUBDIR ${CMAKE_CURRENT_SOURCE_DIR}/include-stubs)

# Generate a patched copy of vcpu.cpp into the build tree.
set(WASABIQT_VCPU_PATCHED ${CMAKE_CURRENT_BINARY_DIR}/vcpu.cpp)
set(WASABIQT_VCPU_PATCH   ${CMAKE_CURRENT_SOURCE_DIR}/patches/vcpu-qtwasabi.patch)
add_custom_command(
    OUTPUT  ${WASABIQT_VCPU_PATCHED}
    COMMAND ${CMAKE_COMMAND} -E copy
            ${WASABI_SRC_DIR}/Wasabi/api/script/vcpu.cpp
            ${WASABIQT_VCPU_PATCHED}
    COMMAND patch -p1 -d ${CMAKE_CURRENT_BINARY_DIR}
            -i ${WASABIQT_VCPU_PATCH}
    DEPENDS ${WASABI_SRC_DIR}/Wasabi/api/script/vcpu.cpp
            ${WASABIQT_VCPU_PATCH}
    COMMENT "Patching vcpu.cpp with qtWasabi mutations"
    VERBATIM)

set(WASABIQT_BFC_SOURCES
    # ── BFC subset ───────────────────────────────────────────────
    # Pure containers / GUID / geometry — POSIX-clean, all compile
    # straight from WASABI_SRC_DIR with the port-shim.
    ${WASABI_SRC_DIR}/Wasabi/bfc/memblock.cpp
    ${WASABI_SRC_DIR}/Wasabi/bfc/foreach.cpp
    ${WASABI_SRC_DIR}/Wasabi/bfc/ptrlist.cpp
    ${WASABI_SRC_DIR}/Wasabi/bfc/stack.cpp
    ${WASABI_SRC_DIR}/Wasabi/bfc/node.cpp
    ${WASABI_SRC_DIR}/Wasabi/bfc/freelist.cpp
    ${WASABI_SRC_DIR}/Wasabi/bfc/wasabi_std_rect.cpp
    ${WASABI_SRC_DIR}/Wasabi/bfc/nsguid.cpp
    # StringW.cpp is NOT compiled directly — upstream lacks a Linux
    # branch in StringW::va_sprintf and falls through with no return
    # (SIGTRAP at the next caller).  StringW-port.cpp #include's it
    # under `#define __APPLE__ 1`, which routes va_sprintf to the
    # POSIX vswprintf path that works on Linux.
    ${CMAKE_CURRENT_SOURCE_DIR}/StringW-port.cpp
    ${WASABI_SRC_DIR}/Wasabi/bfc/string/string.cpp

    # ── Maki VM (opensourced + qtWasabi patch) ─────────────────────────
    # vcpu.cpp is the bit-perfect bytecode dispatch loop.  We copy the
    # upstream source into the build tree and apply patches/vcpu-
    # qtwasabi.patch on top — small mutations for opcode + CALLM
    # tracing, configDummy fallback, and accepting any object-shaped
    # receiver in callDLF.  Its header neighbours scriptmgr.cpp /
    # script.cpp are integration glue that pulls the entire skin/widget
    # tree and is therefore *not* vendored — WasabiQT writes its own
    # thin manager that wires the VM to the WasabiQt::Skin widget tree.
    ${WASABIQT_VCPU_PATCHED}

    # ── Linux/Qt implementations ─────────────────────────────────
    # Platform hooks BFC declares but only provides on Win32 / macOS.
    ${CMAKE_CURRENT_SOURCE_DIR}/wasabi-port-stubs.cpp

    # Link-time bodies for the upstream classes our include-stubs/
    # headers declare.  Returning-default implementations so the
    # opensourced vcpu.cpp links into libwasabiqt.  WasabiQT-own code
    # later supplies the real bodies (script bindings → Qt widgets).
    ${CMAKE_CURRENT_SOURCE_DIR}/wasabi-port-link-stubs.cpp

    # Bridge between the opensourced VCPU class and the rest of WasabiQT.
    # Sole TU that includes <api/script/vcpu.h> — limits the blast
    # radius of bfc/platform/linux.h's macro pollution.
    ${CMAKE_CURRENT_SOURCE_DIR}/maki-bridge.cpp

    # WidgetScriptObject: ScriptObject subclass backed by an opaque
    # widget pointer.  Sole TU that #include's <api/script/scriptobj.h>.
    ${CMAKE_CURRENT_SOURCE_DIR}/widget-script-object.cpp

    # Maki method bodies — function pointers that ObjectTable::
    # addrefDLF wires into VCPUdlfEntry::ptr so CALLMs run real code.
    ${CMAKE_CURRENT_SOURCE_DIR}/maki-bindings.cpp
)

# Object library — the .o files are pulled into both the shared and the
# static libwasabiqt without rebuilding twice.  Downstream targets in
# src/ refer to it via the WASABIQT_PORT_OBJECTS variable below.
add_library(wasabiqt_port OBJECT ${WASABIQT_BFC_SOURCES})

set(WASABIQT_PORT_INCLUDES
    # include-stubs/ FIRST so our overrides for entangled windowing
    # headers (Canvas.h, api_timer.h) win over WASABI_SRC_DIR.
    ${WASABIQT_PORT_STUBDIR}
    # vcpu.cpp lives in our build tree (patched copy), but does
    # `#include "vcpu.h"` — its upstream sibling, which we don't
    # patch.  Add the script/ dir directly so that "" lookup finds it.
    ${WASABI_SRC_DIR}/Wasabi/api/script
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
