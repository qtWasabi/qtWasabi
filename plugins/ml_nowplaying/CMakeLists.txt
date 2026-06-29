# ml_nowplaying — port of Winamp's ml_nowplaying plugin.
#
# Build strategy: symlink-based staging.  ml_nowplaying's source
# files use relative includes like `#include "../gen_ml/ml.h"` that
# expect a `Plugins/Library/gen_ml/` sibling.  gen_ml actually
# lives at `Plugins/General/gen_ml/`.  Modifying the upstream tree
# to "fix" that path would break upstream rebases.
#
# Instead we mirror the WHOLE `Plugins/Library/` directory into the
# build tree at configure time as symlinks, then ALSO drop a symlink
# `gen_ml -> ../General/gen_ml/`.  The build tree's Plugins/Library/
# layout now has every relative include resolving correctly without
# touching upstream source.

if(NOT DEFINED WASABI_SRC_DIR OR NOT EXISTS ${WASABI_SRC_DIR})
    message(FATAL_ERROR
        "WASABI_SRC_DIR not set or doesn't exist; can't build ml_nowplaying")
endif()

# Staging root — mirrors `Plugins/` from WASABI_SRC_DIR.
set(QTWASABI_MLNP_STAGE  ${CMAKE_CURRENT_BINARY_DIR}/staged)

# Symlink-mirror upstream's Plugins/Library and Plugins/General
# at configure time.  The mirror is one-deep — each subdir is a
# symlink pointing at the real directory in WASABI_SRC_DIR.  Any
# upstream edit is reflected immediately on the next build (no
# stale staged copy).
file(MAKE_DIRECTORY ${QTWASABI_MLNP_STAGE}/Plugins/Library
                    ${QTWASABI_MLNP_STAGE}/Plugins/General)

# Library subdirs we want available to ml_nowplaying.
foreach(_dir IN ITEMS ml_nowplaying ml_online nu)
    set(_target ${QTWASABI_MLNP_STAGE}/Plugins/Library/${_dir})
    if(NOT EXISTS ${_target})
        file(CREATE_LINK ${WASABI_SRC_DIR}/Plugins/Library/${_dir}
                         ${_target} SYMBOLIC)
    endif()
endforeach()

# gen_ml + winamp from General.  We expose gen_ml at BOTH its real
# General/ location and at the Library/ alias the source expects.
foreach(_dir IN ITEMS gen_ml winamp)
    set(_target ${QTWASABI_MLNP_STAGE}/Plugins/General/${_dir})
    if(NOT EXISTS ${_target})
        file(CREATE_LINK ${WASABI_SRC_DIR}/Plugins/General/${_dir}
                         ${_target} SYMBOLIC)
    endif()
endforeach()

# The crucial alias: Library/gen_ml -> ../General/gen_ml.  Makes
# `#include "../gen_ml/ml.h"` from ml_nowplaying/main.h resolve.
set(_genml_alias ${QTWASABI_MLNP_STAGE}/Plugins/Library/gen_ml)
if(NOT EXISTS ${_genml_alias})
    file(CREATE_LINK ${WASABI_SRC_DIR}/Plugins/General/gen_ml
                     ${_genml_alias} SYMBOLIC)
endif()

# Also need Src/ at the top so deeper includes like `<api/service/
# api_service.h>` (which actually go through angle brackets and
# search include paths) resolve.

# Replace the symlinked ml_nowplaying with REAL COPIES — the
# preprocessor's realpath() follows a symlinked .cpp to the
# upstream tree where `../gen_ml/` doesn't resolve.  Copies in the
# build tree make `..` resolve against the build tree where the
# gen_ml alias symlink exists.  configure_file() with COPYONLY
# means upstream changes trigger a CMake re-run.
file(REMOVE ${QTWASABI_MLNP_STAGE}/Plugins/Library/ml_nowplaying)
file(MAKE_DIRECTORY ${QTWASABI_MLNP_STAGE}/Plugins/Library/ml_nowplaying)

set(_mlnp_src_files
    main.cpp main.h
    common.cpp common.h
    external.cpp external.h
    handler.cpp handler.h
    local_menu.cpp local_menu.h
    navigation.cpp navigation.h
    service.cpp service.h
    wasabi.cpp wasabi.h
    wasabiCallback.cpp wasabiCallback.h
    resource.h)
foreach(_f IN LISTS _mlnp_src_files)
    set(_upstream
        ${WASABI_SRC_DIR}/Plugins/Library/ml_nowplaying/${_f})
    set(_staged
        ${QTWASABI_MLNP_STAGE}/Plugins/Library/ml_nowplaying/${_f})
    if(_f STREQUAL "wasabi.h" AND EXISTS ${_upstream})
        # wasabi.h has an `##` token-paste operator that GCC
        # rejects.  Sed-correct it during the staging copy.
        file(READ ${_upstream} _wasabi_h_content)
        string(REPLACE "##__interfaceType" "__interfaceType"
                       _wasabi_h_corrected "${_wasabi_h_content}")
        file(WRITE ${_staged} "${_wasabi_h_corrected}")
    elseif(EXISTS ${_upstream})
        configure_file(${_upstream} ${_staged} COPYONLY)
    endif()
endforeach()

# ── Upstream-header path-correction shim ────────────────────────
# Some upstream headers reference Win32-only path forms that don't
# survive on case-sensitive Linux + the case mismatch between disk
# (`Src/Winamp/`) and the upstream code's `../winamp/`.  Build a
# corrected mirror of each problem header in `${QTWASABI_MLNP_STAGE}/
# upstream-corrected/` with backslashes flipped + casing aligned,
# then put that directory FIRST on the include path so it shadows
# the broken upstream copy.
set(_corrected_root ${QTWASABI_MLNP_STAGE}/upstream-corrected)
file(MAKE_DIRECTORY ${_corrected_root}/nu ${_corrected_root}/Wasabi)

# Case-alias symlinks so `<winamp/...>` resolves against `<Winamp/>`
# disk directory.  Linux is case-sensitive; Win32 isn't.
file(MAKE_DIRECTORY ${_corrected_root}/case-alias)
if(NOT EXISTS ${_corrected_root}/case-alias/winamp)
    file(CREATE_LINK ${WASABI_SRC_DIR}/Winamp
                     ${_corrected_root}/case-alias/winamp SYMBOLIC)
endif()
# Also expose `winamp/` directly inside the corrected root so a
# `../winamp/` relative include from a corrected nu/*.h resolves
# inside our shim tree (otherwise it walks back to upstream where
# the lowercase dir doesn't exist).
if(NOT EXISTS ${_corrected_root}/winamp)
    file(CREATE_LINK ${WASABI_SRC_DIR}/Winamp
                     ${_corrected_root}/winamp SYMBOLIC)
endif()

# Per-file rewrite: backslash → forward slash, then drop into the
# corrected mirror.  Path of the corrected file inside the mirror
# matches its path under Src/ so `-I corrected/` makes
# `#include "nu/MediaLibraryInterface.h"` reach our copy first.
set(_to_correct
    nu/MediaLibraryInterface.h
    nu/DialogSkinner.h
    nu/SendTo.h)

# Autowide.h — our own implementation lives in wasabi-compat/win32/
# (upstream's gates on `#ifdef WIN32` which we can't define without
# cascading into other Win32 branches that need <io.h> etc.).
# Forward both case variants and the upstream path.
file(MAKE_DIRECTORY ${_corrected_root}/replicant/nu)
file(WRITE ${_corrected_root}/replicant/nu/Autowide.h
    "#include <Autowide.h>\n")
file(WRITE ${_corrected_root}/replicant/nu/AutoWide.h
    "#include <Autowide.h>\n")
file(WRITE ${_corrected_root}/nu/AutoWide.h
    "#include <Autowide.h>\n")
file(WRITE ${_corrected_root}/nu/Autowide.h
    "#include <Autowide.h>\n")
file(WRITE ${_corrected_root}/replicant/nu/AutoChar.h
    "#include <Autowide.h>\n")
file(WRITE ${_corrected_root}/nu/AutoChar.h
    "#include <Autowide.h>\n")
foreach(_rel IN LISTS _to_correct)
    set(_src ${WASABI_SRC_DIR}/${_rel})
    set(_dst ${_corrected_root}/${_rel})
    get_filename_component(_dst_dir ${_dst} DIRECTORY)
    file(MAKE_DIRECTORY ${_dst_dir})
    if(EXISTS ${_src})
        # `tr` flips backslash → slash so `..\Plugins\General\gen_ml/ml.h`
        # becomes `../Plugins/General/gen_ml/ml.h`, which then resolves
        # against `_corrected_root` (added below as include dir).
        add_custom_command(
            OUTPUT  ${_dst}
            COMMAND tr '\\\\' '/' < ${_src} > ${_dst}
            DEPENDS ${_src}
            COMMENT "Path-correcting upstream ${_rel}"
            VERBATIM)
    endif()
endforeach()

# Aggregator target so the corrected files materialise before the
# .cpp build steps that include them.
add_custom_target(qtwasabi_mlnp_corrected_headers
    DEPENDS
        ${_corrected_root}/nu/MediaLibraryInterface.h
        ${_corrected_root}/nu/DialogSkinner.h
        ${_corrected_root}/nu/SendTo.h)

# Source files to compile.  Phase E.3 milestone: main.cpp goes
# all the way through the compat layer to a 144 KB object file
# with the canonical `winampGetMediaLibraryPlugin` entry point.
#
# The 8 sibling TUs (common/external/handler/local_menu/navigation/
# service/wasabi/wasabiCallback) each pull in another batch of
# Win32 + COM helpers (IID_IDispatch, _wcsicmp, VARIANT, V_VT, …)
# that need shims.  Each is its own day-budget of careful work.
# Phase E.4+ compiles them one at a time once those shims land.
#
# Until they're integrated, build only main.cpp + a link-stubs.cpp
# layer that satisfies the symbols main.cpp needs from siblings so
# the qtwasabi library can link.  Plugin_Init / Plugin_Quit
# entry points remain undefined at this stage — they live in
# main.cpp's siblings.
set(QTWASABI_MLNP_SOURCES
    ${QTWASABI_MLNP_STAGE}/Plugins/Library/ml_nowplaying/main.cpp
    ${QTWASABI_MLNP_STAGE}/Plugins/Library/ml_nowplaying/common.cpp
    ${QTWASABI_MLNP_STAGE}/Plugins/Library/ml_nowplaying/external.cpp
    ${QTWASABI_MLNP_STAGE}/Plugins/Library/ml_nowplaying/wasabi.cpp
    ${QTWASABI_MLNP_STAGE}/Plugins/Library/ml_nowplaying/wasabiCallback.cpp
    ${QTWASABI_MLNP_STAGE}/Plugins/Library/ml_nowplaying/local_menu.cpp
    ${QTWASABI_MLNP_STAGE}/Plugins/Library/ml_nowplaying/handler.cpp
    ${QTWASABI_MLNP_STAGE}/Plugins/Library/ml_nowplaying/navigation.cpp
    ${QTWASABI_MLNP_STAGE}/Plugins/Library/ml_nowplaying/service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/link-stubs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/plugin-glue.cpp
)

# Phase F.2.0: each ml_* plugin is a MODULE (loadable .so, not
# linkable).  Real-Winamp ships ml_*.dll as discrete files;
# we mirror that with .so per plugin loaded via dlopen.  Build
# output lands in ${CMAKE_BINARY_DIR}/qtamp-plugins/ for development;
# users can also drop binaries into ~/.qtamp/plugins/.
add_library(qtwasabi_ml_nowplaying MODULE ${QTWASABI_MLNP_SOURCES})

target_include_directories(qtwasabi_ml_nowplaying PRIVATE
    # Path-corrected upstream headers go FIRST so they shadow the
    # broken originals (backslashes / case mismatch).
    ${_corrected_root}
    ${_corrected_root}/case-alias    # `winamp/` → `Winamp/` shim
    # wasabi-port's include-stubs/ — Canvas.h / api_timer.h /
    # shlwapi.h stubs that the Maki VM build proved out and we
    # can reuse here.  QTWASABI_PORT_STUBDIR is not PARENT_SCOPE'd
    # so we point at the well-known location directly.
    ${CMAKE_CURRENT_SOURCE_DIR}/../../wasabi-port/include-stubs
    ${QTWASABI_COMPAT_INCLUDES}       # win32, services, ml_ipc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../wasabi-compat   # smoke-check.h
    ${WASABI_SRC_DIR}
    ${WASABI_SRC_DIR}/Wasabi
    ${WASABI_SRC_DIR}/Wasabi/bfc
    ${WASABI_SRC_DIR}/omBrowser        # obj_ombrowser.h + IDs
    ${WASABI_SRC_DIR}/Agave            # AGAVE_API_* surface
    ${WASABI_SRC_DIR}/Plugins/General  # gen_ml/ + winamp/ direct
    ${WASABI_SRC_DIR}/Plugins/Library  # nu/, ml_online/
    ${WASABI_SRC_DIR}/replicant)       # replicant/nu/AutoChar.h etc.

# Wait on the path-corrected upstream headers before building.
add_dependencies(qtwasabi_ml_nowplaying
    qtwasabi_mlnp_corrected_headers)

target_compile_options(qtwasabi_ml_nowplaying PRIVATE
    -include ${QTWASABI_COMPAT_SHIM}
    -DLINUX -DUNICODE -D_UNICODE
    # MSVC version pretence — Winamp source gates legacy typedefs
    # on `#if (_MSC_VER <= 1200)`.  GCC has no _MSC_VER, so the
    # condition is trivially true and we hit `typedef int intptr_t`
    # in wa_ipc.h conflicting with stdint.h.  Scope this to the
    # plugin target only — leaking _MSC_VER into wasabi-compat's
    # Qt-including TUs trips qcompilerdetection.h's MSVC version
    # check (requires <intrin.h>).
    -D_MSC_VER=1924
    -fpermissive
    -Wno-register -Wno-write-strings -Wno-narrowing -Wno-overflow
    -Wno-multichar
    -Wno-unused-parameter -Wno-unused-variable)

set_target_properties(qtwasabi_ml_nowplaying PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    # Drop the "lib" prefix — real Winamp ships `ml_nowplaying.dll`,
    # our equivalent is `ml_nowplaying.so` (without "lib").
    PREFIX ""
    OUTPUT_NAME "ml_nowplaying"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qtamp-plugins")

# Plugin .so links against libqtwasabi for the wasabi-compat layer's
# SendMessageW / ImageList_* / handle-registry etc. that the upstream
# Winamp code calls.  At runtime: qtamp loads libqtwasabi, then
# dlopens the plugin which resolves to the already-loaded libqtwasabi.
target_link_libraries(qtwasabi_ml_nowplaying PRIVATE qtwasabi)
