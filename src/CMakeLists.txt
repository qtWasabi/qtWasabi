# src/ — engine integration glue.
#
# Bridges the WasabiQt::Host embedder interface to Wasabi's service
# registry, loads skins via Wasabi's SkinParser, and forwards Maki
# script events back to the host.  The actual VM and widget classes
# come from the user-supplied ${WASABI_SRC_DIR}.

add_library(wasabiqt SHARED
    HostAdapter.cpp
    SkinLoader.cpp
    Bootstrap.cpp
)

target_include_directories(wasabiqt
    PUBLIC
        ${CMAKE_SOURCE_DIR}/public
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${WASABI_SRC_DIR}
        ${WASABI_SRC_DIR}/Wasabi)

target_link_libraries(wasabiqt
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    PRIVATE
        wasabiqt_qt6)

target_compile_features(wasabiqt PUBLIC cxx_std_20)

set_target_properties(wasabiqt PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME wasabiqt)

install(TARGETS wasabiqt
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
