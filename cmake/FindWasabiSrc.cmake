# FindWasabiSrc.cmake — locate the user-supplied Wasabi source tree.
#
# The Wasabi source (from the 2024 Llama Group release, mirrored on
# archive.org) is licensed under the Winamp Collaborative License,
# which forbids redistribution by anyone but Llama Group itself.
# WasabiQT therefore does not ship any of it.  The user provides
# their own copy via:
#
#   1. -DWASABI_SRC_DIR=/path/to/Src  (preferred; explicit)
#   2. WASABI_SRC_DIR environment variable
#   3. ./wasabi-src/Src                (where scripts/fetch-wasabi.sh
#                                       extracts the archive.org tarball
#                                       — local development convention)
#
# After this module runs, ${WASABI_SRC_DIR} points at the directory
# containing api/, bfc/, Lib/, replicant/ etc.

if(NOT WASABI_SRC_DIR)
    if(DEFINED ENV{WASABI_SRC_DIR})
        set(WASABI_SRC_DIR "$ENV{WASABI_SRC_DIR}")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/wasabi-src/Src/Wasabi")
        set(WASABI_SRC_DIR "${CMAKE_SOURCE_DIR}/wasabi-src/Src")
    endif()
endif()

if(NOT WASABI_SRC_DIR OR NOT EXISTS "${WASABI_SRC_DIR}")
    message(FATAL_ERROR
        "
        WasabiQT requires you to supply the Wasabi source tree
        separately.  WasabiQT does not redistribute it (the Winamp
        Collaborative License forbids that).

        Quickest path:
            $ ./scripts/fetch-wasabi.sh
            $ cmake -B build

        Or supply your own copy:
            $ cmake -B build -DWASABI_SRC_DIR=/your/path/to/Src
            $ export WASABI_SRC_DIR=/your/path/to/Src && cmake -B build

        The expected directory is the 'Src/' folder containing the
        Wasabi/ subtree (with api/, bfc/, Lib/, replicant/).

        You currently set WASABI_SRC_DIR=${WASABI_SRC_DIR}
        ")
endif()

# Sanity-check the subdirs we'll actually need to build.
foreach(_required_subdir
    "Wasabi"
    "Wasabi/api"
    "Wasabi/api/script"
    "Wasabi/api/wnd"
    "Wasabi/api/skin"
    "Wasabi/bfc"
    "Wasabi/Lib")
    if(NOT EXISTS "${WASABI_SRC_DIR}/${_required_subdir}")
        message(FATAL_ERROR
            "WASABI_SRC_DIR=${WASABI_SRC_DIR} is missing required "
            "subdirectory '${_required_subdir}'.  Looks like the "
            "tree was extracted incompletely or you pointed at the "
            "wrong location.  Expected layout:\n"
            "    Src/\n"
            "    └─ Wasabi/\n"
            "        ├─ api/        (script/, wnd/, skin/, ...)\n"
            "        ├─ bfc/        (foundation framework)\n"
            "        └─ Lib/        (std.mi, shared Maki includes)\n")
    endif()
endforeach()

set(WASABI_SRC_DIR "${WASABI_SRC_DIR}" CACHE PATH
    "Path to the user-supplied Wasabi source tree" FORCE)

message(STATUS "Located Wasabi source at: ${WASABI_SRC_DIR}")
