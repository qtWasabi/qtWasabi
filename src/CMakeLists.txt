# src/ — engine integration glue.
#
# Builds BOTH a shared library (libwasabiqt.so / .dylib / .dll) for
# system-wide / package-manager installation AND an importable
# static archive (libwasabiqt.a) for app-bundled deployment.
# Downstream consumers pick whichever fits their distribution model.
#
# Bridges the WasabiQt::Host embedder interface to Wasabi's service
# registry, loads skins via Wasabi's SkinParser, and forwards Maki
# script events back to the host.  The actual VM and widget classes
# come from the user-supplied ${WASABI_SRC_DIR}.

set(WASABIQT_SOURCES
    HostAdapter.cpp
    SkinLoader.cpp
    SkinXml.cpp
    Layout.cpp
    BitmapRegistry.cpp
    FontRegistry.cpp
    GammasetRegistry.cpp
    LayerPainter.cpp
    TreePainter.cpp
    TextPainter.cpp
    SkinView.cpp
    ${CMAKE_SOURCE_DIR}/public/WasabiQt/SkinView.h
    Bootstrap.cpp
    # Q_OBJECT public headers — listed so AUTOMOC generates their
    # moc_*.cpp files inside this target (otherwise downstream link
    # complains about missing vtables for Skin etc.)
    ${CMAKE_SOURCE_DIR}/public/WasabiQt/Skin.h
    ${WASABIQT_QT6_SOURCES}
)

# ── shared library ─────────────────────────────────────────────
# WASABIQT_PORT_OBJECTS is a $<TARGET_OBJECTS:wasabiqt_port> generator
# expression — each .o is embedded directly without an intermediate
# static archive.  Set in wasabi-port/CMakeLists.txt.
add_library(wasabiqt SHARED
    ${WASABIQT_SOURCES}
    ${WASABIQT_PORT_OBJECTS})
add_library(WasabiQT::WasabiQT ALIAS wasabiqt)

# ── static library (always built; small, gives embedders choice) ─
add_library(wasabiqt_static STATIC
    ${WASABIQT_SOURCES}
    ${WASABIQT_PORT_OBJECTS})
add_library(WasabiQT::Static ALIAS wasabiqt_static)

# Common configuration for both
foreach(_target wasabiqt wasabiqt_static)
    target_include_directories(${_target}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/public>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${WASABIQT_QT6_INCLUDE_DIR}
            ${WASABI_SRC_DIR}
            ${WASABI_SRC_DIR}/Wasabi)

    target_link_libraries(${_target}
        PUBLIC
            Qt6::Core
            Qt6::Gui
            Qt6::Widgets)

    target_compile_features(${_target} PUBLIC cxx_std_20)
endforeach()

set_target_properties(wasabiqt PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME wasabiqt)

set_target_properties(wasabiqt_static PROPERTIES
    OUTPUT_NAME wasabiqt
    POSITION_INDEPENDENT_CODE ON)

# ── install: targets, headers, exports ─────────────────────────
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

install(TARGETS wasabiqt wasabiqt_static
    EXPORT WasabiQTTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY ${CMAKE_SOURCE_DIR}/public/WasabiQt
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h")

# CMake package config — lets downstream do find_package(WasabiQT).
install(EXPORT WasabiQTTargets
    FILE WasabiQTTargets.cmake
    NAMESPACE WasabiQT::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WasabiQT)

configure_package_config_file(
    ${CMAKE_SOURCE_DIR}/cmake/WasabiQTConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/WasabiQTConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WasabiQT)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/WasabiQTConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/WasabiQTConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/WasabiQTConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WasabiQT)

# pkg-config (.pc) — for autotools / non-CMake consumers.
configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/wasabiqt.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/wasabiqt.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/wasabiqt.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
