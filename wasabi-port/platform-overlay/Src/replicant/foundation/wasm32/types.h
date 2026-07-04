#pragma once
// WebAssembly (Emscripten, wasm32) platform types for Wasabi/replicant.
//
// Upstream never targeted wasm. Emscripten is ILP32 with a 4-byte wchar_t,
// so the fixed-width typedefs below match the linux/osx layouts except for
// pointer width, which nothing in this header encodes.  Part of the qtWasabi
// port, not the mirror; replicant/foundation/types.h (overlay) selects it
// under __EMSCRIPTEN__.
#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>
#include <wchar.h>

// Standard integer types
typedef unsigned int UINT;
typedef signed int SINT;

typedef unsigned char UCHAR;
typedef signed char SCHAR;

// Color types
typedef uint32_t ARGB32;
typedef uint32_t RGB32;

typedef uint32_t ARGB24;
typedef uint32_t RGB24;

typedef uint16_t ARGB16;
typedef uint16_t RGB16;

typedef uint32_t FOURCC;

// Character types - macOS uses UTF-8 filenames; Winamp strings are UTF-16 in
// wide (4-byte wchar_t here, as on the Linux port).
typedef wchar_t nsxml_char_t;
typedef wchar_t ns_char_t;
typedef char nsfilename_char_t;

// GUID structure (compatible with Windows GUID)
#ifndef GUID_DEFINED
#define GUID_DEFINED

typedef struct _GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} GUID;

#endif

// GUID comparison functions
#ifndef GUID_EQUALS_DEFINED
#define GUID_EQUALS_DEFINED

inline bool operator==(const GUID& a, const GUID& b) {
    return a.Data1 == b.Data1 &&
           a.Data2 == b.Data2 &&
           a.Data3 == b.Data3 &&
           a.Data4[0] == b.Data4[0] &&
           a.Data4[1] == b.Data4[1] &&
           a.Data4[2] == b.Data4[2] &&
           a.Data4[3] == b.Data4[3] &&
           a.Data4[4] == b.Data4[4] &&
           a.Data4[5] == b.Data4[5] &&
           a.Data4[6] == b.Data4[6] &&
           a.Data4[7] == b.Data4[7];
}

inline bool operator!=(const GUID& a, const GUID& b) {
    return !(a == b);
}

#define REFGUID const GUID &

#endif
