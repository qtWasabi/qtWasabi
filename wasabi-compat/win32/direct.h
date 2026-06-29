// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once
//
// direct.h — MSVC's POSIX-equivalent filesystem header.  Plugin
// code includes for _chdir / _mkdir / _getcwd / _wchdir.  Linux's
// <unistd.h> + <sys/stat.h> provide everything; just forward.
//

#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

inline int   _chdir(const char *p)            { return ::chdir(p); }
inline int   _mkdir(const char *p)            { return ::mkdir(p, 0755); }
inline int   _rmdir(const char *p)            { return ::rmdir(p); }
inline char *_getcwd(char *buf, int sz)       { return ::getcwd(buf, sz); }
inline int   _wchdir(const wchar_t *)         { return -1; }
inline int   _wmkdir(const wchar_t *)         { return -1; }
inline int   _wrmdir(const wchar_t *)         { return -1; }

#ifdef __cplusplus
}  // extern "C"
#endif
