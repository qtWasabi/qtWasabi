# wasabi-compat/ — Win32 + Wasabi API compatibility layer for porting
# `gen_ml` and `ml_*` plugins onto qtWasabi.
#
# Sibling of wasabi-port/ (BFC + Maki VM).  Where wasabi-port papers
# over the Linux/macOS gaps in the BFC subset we actually compile,
# wasabi-compat goes further: it provides the Win32 typedefs,
# common-control message constants, GDI primitives, and Qt-backed
# Wasabi service implementations that the ported plugins consume.
#
# The plugin source under winamp-linux/Src/Plugins/General/gen_ml/ +
# Plugins/Library/ml_*/ stays untouched.  It compiles by force-including
# this directory's `compat-shim.h` and getting include paths into
# `win32/` so its `#include <windows.h>` etc. land on our shims.

set(QTWASABI_COMPAT_SHIM    ${CMAKE_CURRENT_SOURCE_DIR}/compat-shim.h)
set(QTWASABI_COMPAT_WIN32   ${CMAKE_CURRENT_SOURCE_DIR}/win32)

# Source list grows as Phase A through G land.  Initial scaffold has
# only the headers (no .cpp yet) so the OBJECT target compiles cleanly
# even before any sources exist.  CMake requires at least one source
# for an OBJECT library, so we add a placeholder TU that defines the
# version stamp.
set(QTWASABI_COMPAT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/compat-version.cpp
    # Phase A.2 — Win32 typedef smoke test (static_asserts only).
    ${CMAKE_CURRENT_SOURCE_DIR}/smoke-windef.cpp
    # Phase A.3 — handle registry + its smoke test.
    ${CMAKE_CURRENT_SOURCE_DIR}/win32/handle-registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/smoke-registry.cpp
    # Phase A.4 — SendMessage dispatcher + Win32 windowing helpers
    # (DefWindowProc, GetDlgItem stub, IsWindow, ShowWindow,
    # GWL_USERDATA, GetClientRect, MoveWindow, …).
    ${CMAKE_CURRENT_SOURCE_DIR}/win32/send-message.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/smoke-sendmessage.cpp
    # Phase A.5 — Common Controls (TreeView, ListView, Header,
    # ImageList).  Constants + struct layouts + ImageList stub.
    ${CMAKE_CURRENT_SOURCE_DIR}/win32/commctrl-stubs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/smoke-commctrl.cpp
    # Phase A.6 — GDI primitives backed by QPainter/QImage (Get/
    # ReleaseDC, CreateCompatibleBitmap/CreateDIBSection,
    # SetTextColor/SetBkColor/SetBkMode state, BeginPaint/EndPaint,
    # plus link-time bodies for the drawing calls Phase D fills in).
    ${CMAKE_CURRENT_SOURCE_DIR}/win32/wingdi-stubs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/smoke-gdi.cpp
    # Phase ML.3 — Winamp's REAL wa_dlg skinning impl (WA_DLG_IMPLEMENT),
    # hosted on the GDI raster core.  Needs WASABI_SRC_DIR/Winamp on the
    # include path for wa_dlg.h + wa_ipc.h.  STAGED but gated out of the
    # default build: it compiles cleanly once the GDI object handles
    # (HBITMAP/HBRUSH/HPEN/HFONT/HRGN) interconvert with HGDIOBJ (Winamp
    # assumes void* handles; our shim uses strict DECLARE_HANDLE) + the
    # ~15 missing constants/region-funcs below are added.  That handle
    # change is cross-cutting (ml_* plugins depend on the strict types),
    # so it gets its own focused pass with a full multi-target rebuild.
    ${CMAKE_CURRENT_SOURCE_DIR}/win32/wa-dlg-impl.cpp
    # ML.1 — synthesise the genex theme bitmap (skin colours → genex →
    # wa_dlg GetPixel) + answer IPC_GET_GENSKINBITMAP.
    ${CMAKE_CURRENT_SOURCE_DIR}/win32/genex.cpp
    # ML.3 — render the wa_dlg owner-draw silver button to an ARGB buffer
    # for the engine's MlHostWidget to blit.
    ${CMAKE_CURRENT_SOURCE_DIR}/win32/wadlg-render.cpp
    # Phase A.7 — end-to-end Phase-A verification.  Synthetic
    # minimum-viable Win32 plugin exercising the message dispatch +
    # ImageList + GDI + GWL_USERDATA round-trips gen_ml will hit
    # on boot.  Stays in the OBJECT lib as static-init smoke.
    ${CMAKE_CURRENT_SOURCE_DIR}/smoke-phase-a.cpp
    # Phase B.1 — Wasabi service registry.  Per-service
    # implementations land in B.2 (LNG) → B.7 (DECODE/SYSCB/PALETTE).
    ${CMAKE_CURRENT_SOURCE_DIR}/services/service-registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/smoke-services.cpp
    # Phase B.2 — WASABI_API_LNG passthrough.
    ${CMAKE_CURRENT_SOURCE_DIR}/services/lang-service.cpp
    # Phase B.3 — WASABI_API_APP (paths/DPI/version) +
    # WASABI_API_WND (window class registration stubs).
    ${CMAKE_CURRENT_SOURCE_DIR}/services/app-service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/services/wnd-service.cpp
    # Phase B.4 — WASABI_API_SKIN bridge to qtWasabi::SkinRuntime.
    ${CMAKE_CURRENT_SOURCE_DIR}/services/skin-service.cpp
    # Phase B.5 — AGAVE_API_CONFIG (QSettings-backed).
    ${CMAKE_CURRENT_SOURCE_DIR}/services/config-service.cpp
    # Phase B.6 — AGAVE_API_THREADPOOL (QThreadPool-backed).
    ${CMAKE_CURRENT_SOURCE_DIR}/services/threadpool-service.cpp
    # Phase B.7 — DECODE / SYSCB / PALETTE.
    ${CMAKE_CURRENT_SOURCE_DIR}/services/decode-service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/services/syscb-service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/services/palette-service.cpp
    # Phase B.8 — end-to-end service-init verification.  Mirrors
    # gen_ml::init()'s ServiceBuild() chain and confirms every
    # Wasabi service the plugin queries resolves to a non-null
    # backing.
    ${CMAKE_CURRENT_SOURCE_DIR}/smoke-phase-b.cpp
)

add_library(qtwasabi_compat OBJECT ${QTWASABI_COMPAT_SOURCES})

set(QTWASABI_COMPAT_INCLUDES
    # win32/ FIRST so plugin `#include <windows.h>` resolves here, not
    # to a system <windows.h> (there isn't one on Linux/macOS, but the
    # ordering matters once we add deeper headers).
    ${QTWASABI_COMPAT_WIN32}
    ${CMAKE_CURRENT_SOURCE_DIR}
    CACHE INTERNAL "qtwasabi_compat include paths")

target_include_directories(qtwasabi_compat PRIVATE
    ${QTWASABI_COMPAT_INCLUDES}
    # services/ for the Phase B service registry + per-service
    # interface headers.
    ${CMAKE_CURRENT_SOURCE_DIR}
    # qtWasabi public headers — palette-service.cpp resolves
    # ColorRegistry, skin-service.h forward-declares SkinRuntime.
    ${CMAKE_CURRENT_SOURCE_DIR}/../public
    # Winamp source — wa-dlg-impl.cpp compiles Winamp/wa_dlg.h (which
    # pulls Winamp/wa_ipc.h) directly from the upstream tree.
    ${WASABI_SRC_DIR}/Winamp)

# Qt6 headers — wingdi-stubs.cpp uses QPainter / QImage for the
# HDC / HBITMAP backings.  We only need the include paths; final
# linkage happens through the parent target that consumes the
# OBJECT lib (libqtwasabi).
find_package(Qt6 6.0 REQUIRED COMPONENTS Gui)
target_link_libraries(qtwasabi_compat PRIVATE Qt6::Gui)

# Same warning relaxation wasabi-port uses — upstream Wasabi/Win32
# C++ pre-dates strict GCC defaults.
target_compile_options(qtwasabi_compat PRIVATE
    -include ${QTWASABI_COMPAT_SHIM}
    -DLINUX
    -Wno-register
    -Wno-overflow
    -Wno-cpp
    -Wno-write-strings
    -Wno-narrowing)

set_target_properties(qtwasabi_compat PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

# Expose to parent scope.
set(QTWASABI_COMPAT_OBJECTS  $<TARGET_OBJECTS:qtwasabi_compat>  PARENT_SCOPE)
set(QTWASABI_COMPAT_SHIM     ${QTWASABI_COMPAT_SHIM}            PARENT_SCOPE)
set(QTWASABI_COMPAT_INCLUDES ${QTWASABI_COMPAT_INCLUDES}        PARENT_SCOPE)
