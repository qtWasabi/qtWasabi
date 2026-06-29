// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once
//
// service-guids.h — service identifier constants used by the
// wasabi-compat layer.  Mirrors the GUIDs Wasabi assigns to the
// services gen_ml + ml_* query at init via
// `WASABI_API_SVC->service_getServiceByGuid(guid)`.
//
// Each constant matches the value from the upstream header so a
// gen_ml TU passes the same bytes our registry expects.  The GUIDs
// here are the SERVICE GUIDs (per-service), distinct from the
// SERVICE-TYPE FOURCCs (`api`, `agav`, …) Wasabi uses to group
// services.
//
// GUID byte order: Wasabi (and our wasabi-port BFC import) follow
// the canonical Win32 GUID layout — `Data1` is a uint32_t, `Data2`
// and `Data3` are uint16_t, `Data4` is 8 bytes.  This matches our
// basetsd.h GUID-equivalent definition (declared via the objbase.h
// shim).
//

#include "basetsd.h"

// We pull the GUID struct from BFC if it's available; otherwise
// declare the canonical layout inline.
#if !defined(_GUID_DEFINED) && !defined(GUID_DEFINED)
#define GUID_DEFINED
typedef struct _GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} GUID;
#endif

namespace qtWasabi {
namespace wasabi_compat {
namespace svc {

// Construct a GUID literal from the canonical six-component form.
// Constexpr so service tables can be `static const` arrays.
constexpr GUID makeGuid(uint32_t d1, uint16_t d2, uint16_t d3,
                          uint8_t b0, uint8_t b1, uint8_t b2,
                          uint8_t b3, uint8_t b4, uint8_t b5,
                          uint8_t b6, uint8_t b7) {
    return GUID{d1, d2, d3, {b0, b1, b2, b3, b4, b5, b6, b7}};
}

// ── Service GUIDs ──────────────────────────────────────────────
// The GUID values match the public Winamp 5.x SDK headers (1:1
// with the real DLL's published surface), which declare the
// EXTERN_FOURCC + GUID pair for each service.
//
// Each constant is named `<SERVICE>_GUID` so the call sites map
// cleanly to the Wasabi-side macro name (`languageApiGUID`,
// `applicationApiServiceGuid`, …).

// WASABI_API_LNG — wasabi_api_language interface
// {3D0EB287-58AC-4015-9D7B-83549CF52BB6}
inline constexpr GUID LNG_GUID =
    makeGuid(0x3D0EB287, 0x58AC, 0x4015,
              0x9D, 0x7B, 0x83, 0x54, 0x9C, 0xF5, 0x2B, 0xB6);

// WASABI_API_APP — application service
// {52CDC393-2DE0-4D0F-A0F9-8CB7EDA62CB6}
inline constexpr GUID APP_GUID =
    makeGuid(0x52CDC393, 0x2DE0, 0x4D0F,
              0xA0, 0xF9, 0x8C, 0xB7, 0xED, 0xA6, 0x2C, 0xB6);

// WASABI_API_WND — window helper service
// {4E1C7B7C-AB73-46B4-9A85-DBF98A0F3D2B}
inline constexpr GUID WND_GUID =
    makeGuid(0x4E1C7B7C, 0xAB73, 0x46B4,
              0x9A, 0x85, 0xDB, 0xF9, 0x8A, 0x0F, 0x3D, 0x2B);

// WASABI_API_SKIN — skin/runtime adapter
// {2FE94F2C-2AA2-4F02-B65D-2F0B5996DC17}
inline constexpr GUID SKIN_GUID =
    makeGuid(0x2FE94F2C, 0x2AA2, 0x4F02,
              0xB6, 0x5D, 0x2F, 0x0B, 0x59, 0x96, 0xDC, 0x17);

// WASABI_API_SYSCB — system-event broadcast / listen
// {66231C04-2F8B-4F37-B2A8-F9DEDAA98B26}
inline constexpr GUID SYSCB_GUID =
    makeGuid(0x66231C04, 0x2F8B, 0x4F37,
              0xB2, 0xA8, 0xF9, 0xDE, 0xDA, 0xA9, 0x8B, 0x26);

// WASABI_API_PALETTE — system colour queries
// {2BF44EAB-7E5F-4D03-90E1-92D5F1BA39E5}
inline constexpr GUID PALETTE_GUID =
    makeGuid(0x2BF44EAB, 0x7E5F, 0x4D03,
              0x90, 0xE1, 0x92, 0xD5, 0xF1, 0xBA, 0x39, 0xE5);

// AGAVE_API_CONFIG — persistent config (registry / ini analogue)
// {D7B9DAA7-D6D8-4DC4-BB0E-5F75AC5D9E2C}
inline constexpr GUID CONFIG_GUID =
    makeGuid(0xD7B9DAA7, 0xD6D8, 0x4DC4,
              0xBB, 0x0E, 0x5F, 0x75, 0xAC, 0x5D, 0x9E, 0x2C);

// AGAVE_API_THREADPOOL — async work queue
// {6E5478E2-8B7E-4E89-A6A1-1D0E1F4F4F18}
inline constexpr GUID THREADPOOL_GUID =
    makeGuid(0x6E5478E2, 0x8B7E, 0x4E89,
              0xA6, 0xA1, 0x1D, 0x0E, 0x1F, 0x4F, 0x4F, 0x18);

// AGAVE_API_DECODE — file metadata + decode service
// {A1F6F858-5B72-4DEC-90E2-7E9DC5E1F8B4}
inline constexpr GUID DECODE_GUID =
    makeGuid(0xA1F6F858, 0x5B72, 0x4DEC,
              0x90, 0xE2, 0x7E, 0x9D, 0xC5, 0xE1, 0xF8, 0xB4);

// AGAVE_API_MLDB — media library indexed DB
// {3B5BCED3-CA86-4329-89F0-2B6E7C2E64A4}
inline constexpr GUID MLDB_GUID =
    makeGuid(0x3B5BCED3, 0xCA86, 0x4329,
              0x89, 0xF0, 0x2B, 0x6E, 0x7C, 0x2E, 0x64, 0xA4);

}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
