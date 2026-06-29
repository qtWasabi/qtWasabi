// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#ifndef _BASETSD_H_
#define _BASETSD_H_
//
// basetsd.h — Win32's "base size types" header.  Provides the
// fixed-width integer family Windows code historically used before
// <stdint.h> was common (INT8, UINT8, INT16, …, ULONG_PTR, …).
//
// Our port targets Linux + macOS where <stdint.h> is always
// available; we map the Win32 names directly onto the <stdint.h>
// equivalents so type sizes match the source's expectations
// (INT32 = 32 bits, ULONG_PTR = pointer-wide unsigned).
//

#include <stddef.h>     // size_t, ptrdiff_t
#include <stdint.h>     // int*_t, uint*_t, intptr_t, uintptr_t

#ifdef __cplusplus
extern "C" {
#endif

typedef int8_t        INT8,    *PINT8;
typedef int16_t       INT16,   *PINT16;
typedef int32_t       INT32,   *PINT32;
typedef int64_t       INT64,   *PINT64;
typedef uint8_t       UINT8,   *PUINT8;
typedef uint16_t      UINT16,  *PUINT16;
typedef uint32_t      UINT32,  *PUINT32;
typedef uint64_t      UINT64,  *PUINT64;

typedef int32_t       LONG32,  *PLONG32;
typedef int64_t       LONG64,  *PLONG64;
typedef uint32_t      ULONG32, *PULONG32;
typedef uint64_t      ULONG64, *PULONG64;
typedef uint32_t      DWORD32, *PDWORD32;
typedef uint64_t      DWORD64, *PDWORD64;

typedef intptr_t      INT_PTR,    *PINT_PTR;
typedef uintptr_t     UINT_PTR,   *PUINT_PTR;
typedef intptr_t      LONG_PTR,   *PLONG_PTR;
typedef uintptr_t     ULONG_PTR,  *PULONG_PTR;
typedef uintptr_t     DWORD_PTR,  *PDWORD_PTR;
typedef intptr_t      SSIZE_T,    *PSSIZE_T;
typedef size_t        SIZE_T,     *PSIZE_T;

// HALF_PTR is a 32-on-64 weirdness Windows uses for compatibility
// shims.  Nothing in gen_ml relies on the 32-bit half semantics —
// route to pointer-sized for safety.
typedef intptr_t      HALF_PTR,   *PHALF_PTR;
typedef uintptr_t     UHALF_PTR,  *PUHALF_PTR;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // _BASETSD_H_
