# plugins/ — ported gen_ml + ml_* plugins.  Phase F.2.0 changed each
# from an OBJECT lib (baked into libqtwasabi.so) to a MODULE .so
# (loaded at runtime via dlopen).  This mirrors real-Winamp's DLL
# architecture and lets users drop additional plugins into
# ~/.qtamp/plugins/.
#
# Build outputs land in ${CMAKE_BINARY_DIR}/qtamp-plugins/ — qtamp
# scans that directory plus ~/.qtamp/plugins/ at startup.
#
# Build order: wasabi-compat configures first to set
# QTWASABI_COMPAT_INCLUDES + QTWASABI_COMPAT_SHIM.  libqtwasabi
# (which the plugins link against) must build before plugins.
# CMake handles ordering via target_link_libraries.

add_subdirectory(ml_nowplaying)
add_subdirectory(ml_playlists)

# Plugin MODULE targets aren't linked against qtwasabi/qtamp at
# build time (they're loaded at runtime via dlopen).  Aggregate
# them under one ALL-included custom target so `cmake --build .`
# produces the plugin set.  Embedders that don't want plugins can
# set QTWASABI_BUILD_PLUGINS=OFF.
add_custom_target(qtwasabi_plugins ALL
    DEPENDS
        qtwasabi_ml_nowplaying
        qtwasabi_ml_playlists)

# Plugins build alongside libqtwasabi; nothing else to export.
set(QTWASABI_PLUGIN_OBJECTS "" PARENT_SCOPE)
