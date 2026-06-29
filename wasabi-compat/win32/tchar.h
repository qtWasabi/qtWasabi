// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#ifndef _TCHAR_H_INCLUDED_
#define _TCHAR_H_INCLUDED_
//
// tchar.h — Win32's TCHAR macro family.  When UNICODE / _UNICODE
// is defined, TCHAR maps to wchar_t and the _T() / _TEXT() literal
// wrappers add an L prefix.  Without those macros they map to char
// and pass strings through verbatim.
//
// Plugin source mixes the two; we honour whichever the build
// configures.  ml_nowplaying compiles with `-DUNICODE -D_UNICODE`
// so the wide-string branch is the relevant one.
//

#include "windef.h"

#include <cstring>
#include <cstdio>
#include <cwchar>
#include <cwctype>

#if defined(UNICODE) || defined(_UNICODE)
typedef wchar_t TCHAR;
#  define _T(x)       L ## x
#  define _TEXT(x)    L ## x
#  define __TEXT(x)   L ## x
#  define _tcscpy     wcscpy
#  define _tcsncpy    wcsncpy
#  define _tcscat     wcscat
#  define _tcsncat    wcsncat
#  define _tcscmp     wcscmp
#  define _tcsncmp    wcsncmp
#  define _tcsicmp    wcscasecmp
#  define _tcsnicmp   wcsncasecmp
#  define _tcsstr     wcsstr
#  define _tcschr     wcschr
#  define _tcsrchr    wcsrchr
#  define _tcslen     wcslen
#  define _tcsdup     wcsdup
#  define _tcstol     wcstol
#  define _ttoi(s)    (int)wcstol((s), nullptr, 10)
#  define _tsprintf   swprintf
#  define _tprintf    wprintf
#  define _tfopen     _wfopen
#  define _stprintf   swprintf
#  define _sntprintf  swprintf
#  define _vsntprintf vswprintf
#else
typedef char TCHAR;
#  define _T(x)       x
#  define _TEXT(x)    x
#  define __TEXT(x)   x
#  define _tcscpy     strcpy
#  define _tcsncpy    strncpy
#  define _tcscat     strcat
#  define _tcsncat    strncat
#  define _tcscmp     strcmp
#  define _tcsncmp    strncmp
#  define _tcsicmp    strcasecmp
#  define _tcsnicmp   strncasecmp
#  define _tcsstr     strstr
#  define _tcschr     strchr
#  define _tcsrchr    strrchr
#  define _tcslen     strlen
#  define _tcsdup     strdup
#  define _tcstol     strtol
#  define _ttoi       atoi
#  define _tsprintf   sprintf
#  define _tprintf    printf
#  define _tfopen     fopen
#  define _stprintf   sprintf
#  define _sntprintf  snprintf
#  define _vsntprintf vsnprintf
#endif

// LPTSTR / LPCTSTR — TCHAR pointer pair Win32 widely uses.
#ifndef LPTSTR
#  define LPTSTR  TCHAR *
#endif
#ifndef LPCTSTR
#  define LPCTSTR const TCHAR *
#endif

#endif  // _TCHAR_H_INCLUDED_
