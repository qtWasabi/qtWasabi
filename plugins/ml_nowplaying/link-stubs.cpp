// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// link-stubs.cpp — link-time placeholders.
//
// Each symbol here is referenced by one of ml_nowplaying's
// translation units but lives in code we haven't ported yet (media
// library, config, and related plugin subsystems).
//
// Strategy: provide minimal class declarations with the same
// fully-qualified name + matching signature, then define the
// member functions out-of-line.  C++ name mangling depends ONLY
// on the function name, scope, and parameter types — class layout
// (data members) doesn't affect symbol names.  The referencing TUs
// see the real class shape via their header includes; we see the
// minimum-viable shape needed to host the function bodies the
// linker is looking for.
//
// Function bodies are correctness-preserving stubs: BuildPath
// concatenates as documented, ReadString returns the default,
// menu helpers return null/zero.  Plugin code's existing null
// checks handle the empty-state paths cleanly.
//

#include <strsafe.h>
#include <windows.h>
#include <wtypes.h>
#include <objbase.h>

#include <cstddef>
#include <cstring>

// Canonical COM IIDs now live in libqtwasabi (wasabi-compat/win32/
// send-message.cpp) — shared across all plugin .so files via the
// runtime dynamic linker.  Per-plugin storage would only collide
// when the second plugin loads.

// ── `mediaLibrary` global pointer ───────────────────────────────
// Pointer-shaped service global.  `api_orig_hinstance`,
// `sysCallbackApi`, `languageManager`, `browserManager` are
// provided by the plugin object set; `mediaLibrary` isn't, so we
// define its storage here.
void *mediaLibrary = nullptr;

// ── WasabiApi init / release ────────────────────────────────────
extern "C" HRESULT WasabiApi_Initialize(HINSTANCE /*hInstance*/) {
    return E_NOTIMPL;
}
extern "C" void WasabiApi_Release() {}

// ── MediaLibraryInterface::BuildPath ───────────────────────────
// Wasabi signature: void BuildPath(const wchar_t *pathEnd,
//                                  wchar_t *path, size_t numChars);
// Concatenates pathEnd onto path's existing contents.  This stub
// preserves the caller-supplied path (BuildPath is meant to extend
// a prefix, so leaving an empty string is a benign no-op).
class MediaLibraryInterface {
public:
    void BuildPath(const wchar_t *pathEnd, wchar_t *path, size_t numChars);
};
void MediaLibraryInterface::BuildPath(const wchar_t *pathEnd,
                                      wchar_t *path,
                                      size_t numChars) {
    if (!path || numChars == 0) return;
    if (pathEnd) {
        // Append pathEnd to existing path.
        size_t cur = 0;
        while (cur < numChars && path[cur] != 0) ++cur;
        if (cur >= numChars - 1) return;
        StringCchCopyW(path + cur, numChars - cur, pathEnd);
    }
}

// ── MenuHelper_DuplcateMenu ────────────────────────────────────
// The Wasabi helper deep-copies the menu hierarchy.  This stub
// returns the same handle instead — the caller treats the duplicate
// as equivalent for ownership purposes.  ml_nowplaying never frees
// the result independently of the source, so aliasing is safe here.
HMENU MenuHelper_DuplcateMenu(HMENU hMenu) {
    return hMenu;
}

// ── C_Config ───────────────────────────────────────────────────
// A C_Config reads/writes an INI file via Win32 GetPrivateProfile*.
// This stub's ctor does nothing (it never touches any fields, so the
// real class layout is irrelevant to name mangling), and ReadString
// returns the supplied default — matching Win32 behaviour when the
// key is missing.
class C_Config {
public:
    C_Config(char *path);
    char *ReadString(char *key, char *defaultValue);
};
C_Config::C_Config(char * /*path*/) {}
char *C_Config::ReadString(char * /*key*/, char *defaultValue) {
    return defaultValue;
}

// ── Menu_TrackPopupParam ───────────────────────────────────────
// The real helper invokes TrackPopupMenuEx.  This stub returns -1
// (cancelled), which the caller treats as "user dismissed the
// menu without selecting" — a clean fallback path.
INT Menu_TrackPopupParam(HWND library, HMENU hMenu,
                          UINT fuFlags, INT x, INT y,
                          HWND hwnd, LPTPMPARAMS lptpm,
                          ULONG_PTR param) {
    (void)library; (void)hMenu; (void)fuFlags;
    (void)x; (void)y; (void)hwnd; (void)lptpm; (void)param;
    return -1;
}

// ── MLIF_FILTER3_UID ───────────────────────────────────────────
// The ML IPC header defines this conditionally behind an
// implementation guard set by only one TU.  Provide standalone
// storage using the canonical UUID
// `{721E9E62-CC6D-4fd7-A6ED-DD4CD2B2612E}` from the header.
extern "C" {
extern const GUID MLIF_FILTER3_UID;
const GUID MLIF_FILTER3_UID = {0x721e9e62, 0xcc6d, 0x4fd7,
                                {0xa6, 0xed, 0xdd, 0x4c,
                                 0xd2, 0xb2, 0x61, 0x2e}};
}
