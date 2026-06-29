# ml_playlists — Phase F port of Winamp's ml_playlists plugin.
#
# Reuses every shim ml_nowplaying needed.  Adds a few that
# ml_playlists' broader API surface requires (playlist manager,
# explorer find-file).  CMake structure mirrors ml_nowplaying:
#
#   1. Symlink-stage upstream `Plugins/Library/` and `Plugins/General/`
#      into a build-tree mirror so relative includes resolve.
#   2. configure_file(COPYONLY) real-copy the plugin's own TUs into
#      the staged tree (preprocessor realpath() needs the real
#      build-tree path so `../gen_ml/` resolves against our alias
#      symlink).
#   3. Path-corrected mirror for any header using `\` separators.
#

if(NOT DEFINED WASABI_SRC_DIR OR NOT EXISTS ${WASABI_SRC_DIR})
    message(FATAL_ERROR
        "WASABI_SRC_DIR not set or doesn't exist; can't build ml_playlists")
endif()

set(QTWASABI_MLPL_STAGE  ${CMAKE_CURRENT_BINARY_DIR}/staged)

# Mirror Plugins/{Library,General}/ as symlinks.
file(MAKE_DIRECTORY ${QTWASABI_MLPL_STAGE}/Plugins/Library
                    ${QTWASABI_MLPL_STAGE}/Plugins/General)

foreach(_dir IN ITEMS ml_playlists ml_online nu)
    set(_target ${QTWASABI_MLPL_STAGE}/Plugins/Library/${_dir})
    if(NOT EXISTS ${_target})
        file(CREATE_LINK ${WASABI_SRC_DIR}/Plugins/Library/${_dir}
                         ${_target} SYMBOLIC)
    endif()
endforeach()

foreach(_dir IN ITEMS gen_ml winamp)
    set(_target ${QTWASABI_MLPL_STAGE}/Plugins/General/${_dir})
    if(NOT EXISTS ${_target})
        file(CREATE_LINK ${WASABI_SRC_DIR}/Plugins/General/${_dir}
                         ${_target} SYMBOLIC)
    endif()
endforeach()

# gen_ml alias — Library/gen_ml -> ../General/gen_ml.  Makes
# `#include "../gen_ml/ml.h"` from ml_playlists/main.h resolve.
set(_genml_alias ${QTWASABI_MLPL_STAGE}/Plugins/Library/gen_ml)
if(NOT EXISTS ${_genml_alias})
    file(CREATE_LINK ${WASABI_SRC_DIR}/Plugins/General/gen_ml
                     ${_genml_alias} SYMBOLIC)
endif()

# Replace the ml_playlists symlink with real copies so preprocessor
# realpath() can resolve `../gen_ml/` against the staged tree.
file(REMOVE ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_playlists)
file(MAKE_DIRECTORY ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_playlists)

set(_mlpl_src_files
    # .cpp files
    AddPlaylist.cpp
    # AMNWatcher.cpp deliberately omitted — Windows-only AOL Music
    # Now integration uses HKEY_CURRENT_USER registry + COM that we
    # have no analogue for.  No other TU references its symbols.
    CurrentPlaylist.cpp
    ml_playlists.cpp
    ml_subclass.cpp
    pe_subclass.cpp
    Playlist.cpp
    PlaylistDirectoryCallback.cpp
    PlaylistInfo.cpp
    PlaylistsCB.cpp
    PlaylistsCOM.cpp
    playlists.cpp
    playlistsXML.cpp
    PlaylistView.cpp
    pluginproc.cpp
    RenamePlaylist.cpp
    SendTo.cpp
    view_playlists.cpp
    view_pl.cpp
    wa_subclass.cpp
    # .h files
    main.h
    api__ml_playlists.h
    CurrentPlaylist.h
    PlaylistDirectoryCallback.h
    Playlist.h
    PlaylistInfo.h
    PlaylistsCB.h
    PlaylistsCOM.h
    playlists.h
    playlistsXML.h
    PlaylistView.h
    SendTo.h
    resource.h)
foreach(_f IN LISTS _mlpl_src_files)
    set(_upstream
        ${WASABI_SRC_DIR}/Plugins/Library/ml_playlists/${_f})
    set(_staged
        ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_playlists/${_f})
    if(EXISTS ${_upstream})
        # Read + rewrite backslash-using `#include` paths so Linux's
        # case-sensitive forward-slash filesystem can resolve them.
        # ONLY inside `#include "..."` quoted paths — a blanket
        # backslash replace also corrupts string literals like
        # `L'\''` and `L"foo\nbar"`.
        file(READ ${_upstream} _content)
        # Iteratively rewrite each `\` inside a `#include "..."` to
        # `/`.  Each pass handles ONE backslash per line; loop until
        # none remain.
        set(_passes 0)
        while(_passes LESS 20 AND
              _content MATCHES "(#[ \t]*include[ \t]+[\"<][^\">\r\n]*)\\\\([^\">\r\n]*[\">])")
            string(REGEX REPLACE
                "(#[ \t]*include[ \t]+[\"<][^\">\r\n]*)\\\\([^\">\r\n]*[\">])"
                "\\1/\\2" _content "${_content}")
            math(EXPR _passes "${_passes} + 1")
        endwhile()
        # `extern winampMediaLibraryPlugin plugin;` in main.h has C++
        # linkage, but ml_playlists.cpp defines it with `extern "C"`.
        # GCC strict-checks; MSVC doesn't.  Rewrite the declaration
        # so the linkages agree.
        string(REPLACE
            "extern winampMediaLibraryPlugin plugin;"
            "extern \"C\" winampMediaLibraryPlugin plugin;"
            _content "${_content}")
        file(WRITE ${_staged} "${_content}")
    endif()
endforeach()

# Lowercase-aliased symlinks so `#include "./playlist.h"` (Playlist.cpp)
# and `#include "./amnwatcher.h"` (AMNWatcher.cpp) resolve against
# the real case-correct files.  Linux is case-sensitive; Win32 isn't.
foreach(_pair IN ITEMS
        "amnwatcher.h:AMNWatcher.h"
        "playlist.h:Playlist.h"
        "playlistsCOM.h:PlaylistsCOM.h"
        "playlists.h:playlists.h"
        "playlistview.h:PlaylistView.h"
        "playlistinfo.h:PlaylistInfo.h"
        "currentplaylist.h:CurrentPlaylist.h"
        "playliststcb.h:PlaylistsCB.h"
        "sendto.h:SendTo.h"
        "Main.h:main.h")
    string(REPLACE ":" ";" _pp ${_pair})
    list(GET _pp 0 _lower)
    list(GET _pp 1 _real)
    set(_link
        ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_playlists/${_lower})
    set(_target
        ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_playlists/${_real})
    if(EXISTS ${_target} AND NOT EXISTS ${_link})
        file(CREATE_LINK ${_target} ${_link} SYMBOLIC)
    endif()
endforeach()

# ── Upstream-header path-correction shim (mirrors ml_nowplaying) ─
set(_corrected_root ${QTWASABI_MLPL_STAGE}/upstream-corrected)
file(MAKE_DIRECTORY ${_corrected_root}/nu ${_corrected_root}/Wasabi)

file(MAKE_DIRECTORY ${_corrected_root}/case-alias)
if(NOT EXISTS ${_corrected_root}/case-alias/winamp)
    file(CREATE_LINK ${WASABI_SRC_DIR}/Winamp
                     ${_corrected_root}/case-alias/winamp SYMBOLIC)
endif()
if(NOT EXISTS ${_corrected_root}/winamp)
    file(CREATE_LINK ${WASABI_SRC_DIR}/Winamp
                     ${_corrected_root}/winamp SYMBOLIC)
endif()

# Path-corrected backslash-using headers.
set(_to_correct
    nu/MediaLibraryInterface.h
    nu/DialogSkinner.h
    nu/SendTo.h)

# ml_playlists/main.h uses `#include "../../../nu/AutoLock.h"`.
# Relative path walks up to `${QTWASABI_MLPL_STAGE}/nu/AutoLock.h`.
# Provide a minimal scoped-mutex implementation there using std::mutex.
file(MAKE_DIRECTORY ${QTWASABI_MLPL_STAGE}/nu)
file(WRITE ${QTWASABI_MLPL_STAGE}/nu/AutoLock.h [[
// qtwasabi shim — std::recursive_mutex-backed scoped lock, with the
// Nullsoft::Utility::AutoLockT<T> template upstream ml_playlists uses.
#pragma once
#include <mutex>

class LockGuard {
public:
    std::recursive_mutex m;
    LockGuard() = default;
    LockGuard(unsigned long /*spin*/, const char * = nullptr) {}
    void Lock()   { m.lock(); }
    void Unlock() { m.unlock(); }
};

class AutoLock {
public:
    AutoLock(LockGuard &g, bool = true) : m_g(&g) { g.Lock(); }
    AutoLock(LockGuard *g) : m_g(g) { if (g) g->Lock(); }
    ~AutoLock() { if (m_g) m_g->Unlock(); }
    AutoLock(const AutoLock &) = delete;
    AutoLock &operator=(const AutoLock &) = delete;
private:
    LockGuard *m_g;
};

namespace Nullsoft { namespace Utility {

class LockGuard {
public:
    std::recursive_mutex m;
    void Lock()   { m.lock(); }
    void Unlock() { m.unlock(); }
};

template <class T>
class AutoLockT {
public:
    AutoLockT(T &g) : guard(&g) { g.Lock(); }
    AutoLockT(T *g) : guard(g)  { if (g) g->Lock(); }
    ~AutoLockT() { if (guard) guard->Unlock(); }
    void ManualLock()   { if (guard) guard->Lock(); }
    void ManualUnlock() { if (guard) guard->Unlock(); }
    T *guard;
};

}}  // namespace Nullsoft::Utility
]])

# ml_playlists/main.h includes 3 headers from `ml_downloads/` —
# Downloaded.h (transitive, never used at main.h level),
# DownloadsDialog.h, DownloadStatus.h.  We're not porting ml_downloads
# yet, and one of the upstream headers uses a backslash path that
# breaks on Linux.  Stub them empty so main.h's #include chain
# completes without needing the ml_downloads source.
file(MAKE_DIRECTORY ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_downloads)
foreach(_h IN ITEMS Downloaded.h DownloadStatus.h DownloadsDialog.h)
    file(WRITE
        ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_downloads/${_h}
        "// qtwasabi-stub — Phase F leaves ml_downloads unported\n#pragma once\n")
endforeach()

# ml_local/api_mldb.h is the MLDB Wasabi API.  Upstream header uses
# a `..\..\General\gen_ml/ml.h` backslash include we can't path-correct
# (no .cpp staging for ml_local).  PlaylistView.cpp includes it but
# only uses opaque pointer types.  Stub with minimal forward decls.
file(MAKE_DIRECTORY ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_local)
file(WRITE
    ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_local/api_mldb.h [[
// qtwasabi-stub — Phase F sees opaque pointers only.  PlaylistView.cpp
// references `api_mldb::SYSCALLBACK`, `MLDB_FILE_GET_CLOUD_STATUS`,
// `MLDB_FILE_PROCESS_CLOUD_STATUS` as static const ints.
#pragma once
struct nde_scanner;
class api_mldb {
public:
    static constexpr int SYSCALLBACK                     = 0;
    static constexpr int MLDB_FILE_GET_CLOUD_STATUS      = 0;
    static constexpr int MLDB_FILE_PROCESS_CLOUD_STATUS  = 0;
};
]])

# linux/nxstring.h — replicant/nx/ split-os-arch include.  We don't
# actually need any of its content; an empty file lets the chain
# pass through.
file(MAKE_DIRECTORY ${QTWASABI_MLPL_STAGE}/linux)
file(WRITE
    ${QTWASABI_MLPL_STAGE}/linux/nxstring.h [[
// qtwasabi-stub — nx string opaque + ref-counting noops
#pragma once
struct nx_string_S {
    const wchar_t *string;  // pluginproc.cpp accesses `->string`
    int            _opaque;
};
typedef struct nx_string_S *nx_string_t;
inline nx_string_t NXStringRetain (nx_string_t s) { return s; }
inline void        NXStringRelease(nx_string_t)   {}
inline int         NXStringCreateWithFormatting(nx_string_t *,
                                                  const char *, ...) { return 0; }
inline int         NXStringCreateWithFormattingW(nx_string_t *,
                                                  const wchar_t *, ...) { return 0; }
inline int         NXStringCreateWithUTF8       (nx_string_t *, const char *) { return 0; }
inline int         NXStringCreateWithUTF16      (nx_string_t *, const wchar_t *) { return 0; }
inline const wchar_t *NXStringGetCharactersUTF16(nx_string_t)  { return L""; }
inline const char *NXStringGetCharactersUTF8    (nx_string_t)  { return ""; }
]])
file(WRITE
    ${QTWASABI_MLPL_STAGE}/linux/nxuri.h [[
// qtwasabi-stub — nx uri opaque + ref-counting noops
#pragma once
typedef struct nx_uri_S *nx_uri_t;
inline nx_uri_t NXURIRetain (nx_uri_t u) { return u; }
inline void     NXURIRelease(nx_uri_t)   {}
]])
file(WRITE
    ${QTWASABI_MLPL_STAGE}/linux/nxfile.h
    "// qtwasabi-stub — empty linux/nxfile.h\n#pragma once\n")
file(WRITE
    ${QTWASABI_MLPL_STAGE}/linux/nxapi.h
    "// qtwasabi-stub — empty linux/nxapi.h\n#pragma once\n")
file(WRITE
    ${QTWASABI_MLPL_STAGE}/linux/nxsocket.h
    "// qtwasabi-stub — empty linux/nxsocket.h\n#pragma once\n")
file(WRITE
    ${QTWASABI_MLPL_STAGE}/linux/nxpath.h
    "// qtwasabi-stub — empty linux/nxpath.h\n#pragma once\n")

# replicant/nx/win/* direct includes forward to our linux/ shims.
file(MAKE_DIRECTORY ${QTWASABI_MLPL_STAGE}/replicant/nx/win)
foreach(_h IN ITEMS nxstring nxuri nxapi nxsocket nxpath nxfile)
    file(WRITE
        ${QTWASABI_MLPL_STAGE}/replicant/nx/win/${_h}.h
        "#pragma once\n#include <linux/${_h}.h>\n")
endforeach()

# Same pattern for ml_pmp/pmp.h — opaque pointer use only.  Plus the
# three CLOUD_SOURCE_MENUS_* defines that upstream pmp.h carries and
# pluginproc.cpp / PlaylistView.cpp / view_playlists.cpp reference.
file(MAKE_DIRECTORY ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_pmp)
file(WRITE
    ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_pmp/pmp.h
    "// qtwasabi-stub — Phase F sees opaque pointers only\n#pragma once\nstruct PMPDevicePlugin;\nstruct PMPDevice;\nstruct songid_t;\n#define CLOUD_SOURCE_MENUS              60000\n#define CLOUD_SOURCE_MENUS_UPPER        (60000 + 20)\n#define CLOUD_SOURCE_MENUS_PL_UPPER     (60000 + 200)\n")

# Autowide.h forwarders.
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
        add_custom_command(
            OUTPUT  ${_dst}
            COMMAND tr '\\\\' '/' < ${_src} > ${_dst}
            DEPENDS ${_src}
            COMMENT "Path-correcting upstream ${_rel}"
            VERBATIM)
    endif()
endforeach()

add_custom_target(qtwasabi_mlpl_corrected_headers
    DEPENDS
        ${_corrected_root}/nu/MediaLibraryInterface.h
        ${_corrected_root}/nu/DialogSkinner.h
        ${_corrected_root}/nu/SendTo.h)

set(QTWASABI_MLPL_SOURCES
    ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_playlists/AddPlaylist.cpp
    ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_playlists/CurrentPlaylist.cpp
    ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_playlists/ml_playlists.cpp
    ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_playlists/ml_subclass.cpp
    ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_playlists/pe_subclass.cpp
    ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_playlists/Playlist.cpp
    ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_playlists/PlaylistDirectoryCallback.cpp
    ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_playlists/PlaylistInfo.cpp
    ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_playlists/PlaylistsCB.cpp
    ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_playlists/PlaylistsCOM.cpp
    ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_playlists/playlists.cpp
    # playlistsXML.cpp omitted — references xml/api_xml.h that
    # upstream split into `xml/api__xml.h` (two underscores) +
    # `xml/api_xmlreadercallback.h` that doesn't exist anywhere.
    # No other TU uses the PlaylistsXML class.
    ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_playlists/PlaylistView.cpp
    ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_playlists/pluginproc.cpp
    ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_playlists/RenamePlaylist.cpp
    ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_playlists/SendTo.cpp
    # view_playlists.cpp omitted — playlist LIST window (1952 LoC).
    # Calls api_application methods (app_addAccelerators, getScaleX/Y)
    # whose declarations don't reach our included api_application.h
    # version.  Tooltip + ShellOp surfaces also needed.  Phase F.1
    # focuses on Navigation_Initialize path; view_playlists' dialog
    # proc is stubbed in link-stubs.cpp.
    # view_pl.cpp omitted — playlist EDITOR window (2884 LoC).
    # Pulls in nswasabi/ReferenceCounted.h + nu/nxstring/nxuri/nxapi
    # cascade we don't have headers for, plus WAT/wa_logger.h.
    # Phase F's nav-tree functionality doesn't need the editor;
    # pluginproc.cpp's reference to view_playlistDialogProc is
    # stubbed in link-stubs.cpp.
    ${QTWASABI_MLPL_STAGE}/Plugins/Library/ml_playlists/wa_subclass.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/link-stubs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/plugin-glue.cpp
)

# Phase F.2.0: per-plugin .so (loadable MODULE), not OBJECT.
add_library(qtwasabi_ml_playlists MODULE ${QTWASABI_MLPL_SOURCES})

target_include_directories(qtwasabi_ml_playlists PRIVATE
    ${_corrected_root}
    ${_corrected_root}/case-alias
    # Staged dir comes BEFORE upstream Plugins/Library so our
    # ml_downloads stubs win over the broken upstream headers.
    # `${QTWASABI_MLPL_STAGE}` itself is on the path so our nu/AutoLock.h
    # shim wins over upstream's /Src/nu/AutoLock.h.
    ${QTWASABI_MLPL_STAGE}
    ${QTWASABI_MLPL_STAGE}/Plugins/Library
    ${QTWASABI_MLPL_STAGE}/Plugins/General
    ${CMAKE_CURRENT_SOURCE_DIR}/../../wasabi-port/include-stubs
    ${QTWASABI_COMPAT_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../wasabi-compat
    ${WASABI_SRC_DIR}
    ${WASABI_SRC_DIR}/Wasabi
    ${WASABI_SRC_DIR}/Wasabi/bfc
    ${WASABI_SRC_DIR}/omBrowser
    ${WASABI_SRC_DIR}/Agave
    ${WASABI_SRC_DIR}/Plugins/General
    ${WASABI_SRC_DIR}/Plugins/Library
    ${WASABI_SRC_DIR}/replicant)

add_dependencies(qtwasabi_ml_playlists
    qtwasabi_mlpl_corrected_headers)

target_compile_options(qtwasabi_ml_playlists PRIVATE
    -include ${QTWASABI_COMPAT_SHIM}
    -DLINUX -DUNICODE -D_UNICODE
    -D_MSC_VER=1924
    -fpermissive
    -Wno-register -Wno-write-strings -Wno-narrowing -Wno-overflow
    -Wno-multichar
    -Wno-unused-parameter -Wno-unused-variable)

set_target_properties(qtwasabi_ml_playlists PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    PREFIX ""
    OUTPUT_NAME "ml_playlists"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qtamp-plugins")

target_link_libraries(qtwasabi_ml_playlists PRIVATE qtwasabi)
