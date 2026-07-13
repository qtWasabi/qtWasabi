# src/ — engine integration glue.
#
# Builds BOTH a shared library (libqtwasabi.so / .dylib / .dll) for
# system-wide / package-manager installation AND an importable
# static archive (libqtwasabi.a) for app-bundled deployment.
# Downstream consumers pick whichever fits their distribution model.
#
# Bridges the qtWasabi::Host embedder interface to Wasabi's service
# registry, loads skins via Wasabi's SkinParser, and forwards Maki
# script events back to the host.  The actual VM and widget classes
# come from the user-supplied ${WASABI_SRC_DIR}.

set(QTWASABI_SOURCES
    HostAdapter.cpp
    SkinLoader.cpp
    SkinXml.cpp
    Layout.cpp
    BitmapRegistry.cpp
    CfgAttribStore.cpp
    ColorRegistry.cpp
    FontRegistry.cpp
    GammasetRegistry.cpp
    SkinRuntime.cpp
    SkinRuntimeBridge.cpp
    WindowHolderRegistry.cpp
    LayerPainter.cpp
    ml/MlHostWidget.cpp
    ml/MlLibraryWindow.cpp
    ml/HeaderWindow.cpp
    ml/ListViewWindow.cpp
    ml/TreeViewWindow.cpp
    ml/smoke-phase-d.cpp
    ml/smoke-phase-e.cpp
    TreePainter.cpp
    TextPainter.cpp
    # ── Widget class hierarchy (Phase 1+: class-per-tag refactor) ──
    # Each file under widgets/ implements one or more closely-related
    # widget tags as Widget subclasses with virtual paint/hitTest.
    # Phase 1 lands just the base + factory; subsequent phases migrate
    # paint logic out of TreePainter::paintRecursive into these files.
    widgets/Widget.cpp
    widgets/AlbumArt.cpp
    widgets/AnimatedLayer.cpp
    widgets/Button.cpp
    widgets/ColorThemesList.cpp
    widgets/ComponentBucket.cpp
    widgets/Container.cpp
    widgets/Edit.cpp
    widgets/Gradient.cpp
    widgets/Grid.cpp
    widgets/Images.cpp
    widgets/Browser.cpp
    widgets/CheckBox.cpp
    widgets/DropDownList.cpp
    widgets/EqVis.cpp
    widgets/GuiList.cpp
    widgets/Layer.cpp
    widgets/LayoutStatus.cpp
    widgets/MediaLibraryPanel.cpp
    widgets/Menu.cpp
    widgets/Milkdrop.cpp
    widgets/PlaylistPro.cpp
    widgets/Popup.cpp
    widgets/ProgressGrid.cpp
    widgets/RadioGroup.cpp
    widgets/ScrollBar.cpp
    widgets/Splitter.cpp
    widgets/TabSheet.cpp
    widgets/TreeList.cpp
    widgets/MultiColumnList.cpp
    widgets/Rect.cpp
    widgets/SectionFrame.cpp
    widgets/Slider.cpp
    widgets/Status.cpp
    widgets/Text.cpp
    widgets/Vis.cpp
    widgets/WindowHolder.cpp
    # Phase PL — real in-player playlist renderer (Qt side; the win32
    # draw_pe + shim are compiled separately as qtwasabi_pledit below).
    pledit/PleditHostRenderer.cpp
    SkinView.cpp
    SkinQuickItem.cpp
    Host.cpp
    ${PROJECT_SOURCE_DIR}/public/qtWasabi/SkinView.h
    ${PROJECT_SOURCE_DIR}/public/qtWasabi/SkinQuickItem.h
    Bootstrap.cpp
    # Q_OBJECT public headers — listed so AUTOMOC generates their
    # moc_*.cpp files inside this target (otherwise downstream link
    # complains about missing vtables for Skin etc.)
    ${PROJECT_SOURCE_DIR}/public/qtWasabi/Skin.h
    ${PROJECT_SOURCE_DIR}/public/qtWasabi/PlayerHost.h
    ${QTWASABI_QT6_SOURCES}
)

# ── qtwasabi_pledit — Winamp's REAL draw_pe.cpp playlist renderer +
# the PlayList_*/draw.h shim, compiled with the ml_*-style Winamp env
# (own OBJECT lib so its -D_MSC_VER / -include compat-shim flags don't
# leak into the Qt sources).  Linked into qtwasabi below.
add_library(qtwasabi_pledit OBJECT
    ${CMAKE_CURRENT_SOURCE_DIR}/pledit/stage/draw_pe.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pledit/PleditHostShim.cpp)
target_include_directories(qtwasabi_pledit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/pledit/stage           # trimmed api.h wins
    ${CMAKE_CURRENT_SOURCE_DIR}/pledit                 # PleditHostShim.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../wasabi-compat/win32
    ${CMAKE_CURRENT_SOURCE_DIR}/../wasabi-compat
    ${CMAKE_CURRENT_SOURCE_DIR}/../wasabi-port/include-stubs
    ${PROJECT_SOURCE_DIR}/public
    ${WASABI_SRC_DIR}
    ${WASABI_SRC_DIR}/Wasabi
    ${WASABI_SRC_DIR}/Wasabi/bfc
    ${WASABI_SRC_DIR}/Winamp
    ${WASABI_SRC_DIR}/Plugins/Input/in_wv)
target_compile_options(qtwasabi_pledit PRIVATE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/../wasabi-compat/compat-shim.h
    -DLINUX -DUNICODE -D_UNICODE -D_MSC_VER=1924 -fpermissive
    -Wno-register -Wno-write-strings -Wno-narrowing -Wno-overflow
    -Wno-multichar -Wno-unused-parameter -Wno-unused-variable)
set_target_properties(qtwasabi_pledit PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ── qtwasabi_remote — the remote client family (qtWasabi::remote):
# RemoteHost over an abstract transport (HTTP legacy, GraphQL over
# HTTP/unix socket), the SSE decoder and the shared state document,
# plus FakeHost (the deterministic scripted host).  Separate static
# lib so the engine itself stays free of Qt6::Network.
add_library(qtwasabi_remote STATIC
    remote/RemoteState.cpp
    remote/SseReader.cpp
    remote/RemoteTransport.cpp
    remote/GraphQLTransport.cpp
    remote/RemoteHost.cpp
    # Q_OBJECT public headers for AUTOMOC (as above)
    ${PROJECT_SOURCE_DIR}/public/qtWasabi/remote/RemoteTransport.h
    ${PROJECT_SOURCE_DIR}/public/qtWasabi/remote/GraphQLTransport.h
    ${PROJECT_SOURCE_DIR}/public/qtWasabi/remote/RemoteHost.h
    ${PROJECT_SOURCE_DIR}/public/qtWasabi/FakeHost.h)
add_library(qtWasabi::Remote ALIAS qtwasabi_remote)
target_include_directories(qtwasabi_remote
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/public>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(qtwasabi_remote
    PUBLIC Qt6::Core Qt6::Gui Qt6::Network)
target_compile_features(qtwasabi_remote PUBLIC cxx_std_20)
set_target_properties(qtwasabi_remote PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

# ── qtwasabi_head — the head framework (Qt widgets around the
# engine): skin-derived chrome styling, growing toward HeadWindow/
# HeadMenu/Preferences through the V5 sub-milestones.
add_library(qtwasabi_head STATIC
    head/HeadChrome.cpp
    head/HeadMenu.cpp
    head/HeadPreferences.cpp
    head/HeadTransport.cpp
    head/Orchestrator.cpp
    ${PROJECT_SOURCE_DIR}/public/qtWasabi/head/Orchestrator.h
    ${PROJECT_SOURCE_DIR}/public/qtWasabi/head/HeadPreferences.h
    head/HeadWindow.cpp
    head/HeadWiring.cpp
    ${PROJECT_SOURCE_DIR}/public/qtWasabi/head/HeadWindow.h)
add_library(qtWasabi::Head ALIAS qtwasabi_head)
target_include_directories(qtwasabi_head
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/public>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(qtwasabi_head
    PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick qtwasabi_remote)
target_compile_features(qtwasabi_head PUBLIC cxx_std_20)
set_target_properties(qtwasabi_head PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

# ── qtwasabi_serve — the framework's stock player-protocol server
# (api/player.proto v1 over gRPC on a unix socket, backed by a
# PlayerHost).  Built only where grpc++ exists; the wasm/head-only
# builds never need it.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(GRPCPP IMPORTED_TARGET grpc++)
    pkg_check_modules(SERVE_PROTOBUF IMPORTED_TARGET protobuf)
endif()
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin)
if(GRPCPP_FOUND AND SERVE_PROTOBUF_FOUND AND GRPC_CPP_PLUGIN)
    set(PLAYER_PROTO ${PROJECT_SOURCE_DIR}/api/player.proto)
    set(PLAYER_PROTO_GEN ${CMAKE_CURRENT_BINARY_DIR}/player-proto)
    file(MAKE_DIRECTORY ${PLAYER_PROTO_GEN})
    add_custom_command(
        OUTPUT ${PLAYER_PROTO_GEN}/player.pb.cc ${PLAYER_PROTO_GEN}/player.pb.h
               ${PLAYER_PROTO_GEN}/player.grpc.pb.cc ${PLAYER_PROTO_GEN}/player.grpc.pb.h
        COMMAND protoc --proto_path=${PROJECT_SOURCE_DIR}/api
                --cpp_out=${PLAYER_PROTO_GEN} --grpc_out=${PLAYER_PROTO_GEN}
                --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
                ${PLAYER_PROTO}
        DEPENDS ${PLAYER_PROTO}
        VERBATIM)
    add_library(qtwasabi_serve STATIC
        serve/SidecarService.cpp
        ${PLAYER_PROTO_GEN}/player.pb.cc
        ${PLAYER_PROTO_GEN}/player.grpc.pb.cc)
    add_library(qtWasabi::Serve ALIAS qtwasabi_serve)
    target_include_directories(qtwasabi_serve
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/public>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE ${PLAYER_PROTO_GEN})
    target_link_libraries(qtwasabi_serve
        PUBLIC Qt6::Core Qt6::Gui
        PRIVATE PkgConfig::GRPCPP PkgConfig::SERVE_PROTOBUF)
    target_compile_features(qtwasabi_serve PUBLIC cxx_std_20)
    set_target_properties(qtwasabi_serve PROPERTIES
        POSITION_INDEPENDENT_CODE ON)
    # Generated protobuf code under -Wall is noisy; keep it quiet.
    target_compile_options(qtwasabi_serve PRIVATE -Wno-unused-parameter)
else()
    message(STATUS "grpc++/protobuf/grpc_cpp_plugin not found — qtwasabi_serve (player protocol) disabled")
endif()

# ── shared library ─────────────────────────────────────────────
# QTWASABI_PORT_OBJECTS is a $<TARGET_OBJECTS:qtwasabi_port> generator
# expression — each .o is embedded directly without an intermediate
# static archive.  Set in wasabi-port/CMakeLists.txt.
add_library(qtwasabi SHARED
    ${QTWASABI_SOURCES}
    ${QTWASABI_PORT_OBJECTS}
    ${QTWASABI_COMPAT_OBJECTS}
    $<TARGET_OBJECTS:qtwasabi_pledit>)
# Plugins no longer bake in — each ml_* is its own .so loaded via
# dlopen at runtime.  See plugins/CMakeLists.txt for the rationale.
add_library(qtWasabi::qtWasabi ALIAS qtwasabi)

# ── static library (always built; small, gives embedders choice) ─
add_library(qtwasabi_static STATIC
    ${QTWASABI_SOURCES}
    ${QTWASABI_PORT_OBJECTS}
    ${QTWASABI_COMPAT_OBJECTS}
    $<TARGET_OBJECTS:qtwasabi_pledit>)
add_library(qtWasabi::Static ALIAS qtwasabi_static)

# Common configuration for both
foreach(_target qtwasabi qtwasabi_static)
    target_include_directories(${_target}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/public>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${QTWASABI_QT6_INCLUDE_DIR}
            ${WASABI_SRC_DIR}
            ${WASABI_SRC_DIR}/Wasabi
            # wasabi-compat — Phase D ml/ bridges need
            # handle-registry.h / winuser.h / commctrl.h.
            ${QTWASABI_COMPAT_INCLUDES})

    target_link_libraries(${_target}
        PUBLIC
            Qt6::Core
            Qt6::Gui
            Qt6::Widgets
            Qt6::Quick
            Qt6::Qml
        PRIVATE
            ${CMAKE_DL_LIBS})  # dlopen / dlsym / dlclose

    # Phase F.2.0: plugins are loaded via dlopen from a build-tree
    # directory at runtime.  Pass that path through as a compile def
    # so `loadBuiltinMlPlugins()` knows where to look.
    target_compile_definitions(${_target} PRIVATE
        QTAMP_BUILTIN_PLUGIN_DIR="${CMAKE_BINARY_DIR}/qtamp-plugins")

    target_compile_features(${_target} PUBLIC cxx_std_20)
endforeach()

set_target_properties(qtwasabi PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME qtwasabi)

set_target_properties(qtwasabi_static PROPERTIES
    OUTPUT_NAME qtwasabi
    POSITION_INDEPENDENT_CODE ON)

# ── install: targets, headers, exports ─────────────────────────
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

install(TARGETS qtwasabi qtwasabi_static
    EXPORT qtWasabiTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY ${PROJECT_SOURCE_DIR}/public/qtWasabi
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h")

# CMake package config — lets downstream do find_package(qtWasabi).
install(EXPORT qtWasabiTargets
    FILE qtWasabiTargets.cmake
    NAMESPACE qtWasabi::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qtWasabi)

configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/cmake/qtWasabiConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/qtWasabiConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qtWasabi)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/qtWasabiConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/qtWasabiConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/qtWasabiConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qtWasabi)

# pkg-config (.pc) — for autotools / non-CMake consumers.
configure_file(
    ${PROJECT_SOURCE_DIR}/cmake/qtwasabi.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/qtwasabi.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/qtwasabi.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
