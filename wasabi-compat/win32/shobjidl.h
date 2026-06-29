// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// shobjidl.h — stub for the Windows shell-objects COM header.
// Winamp/Main.h pulls it (taskbar progress / shell item interfaces);
// the in-player playlist render path (draw_pe.cpp) doesn't use any of
// it, so resolving the include is enough.  Add interface decls here
// only if a ported TU actually references one.

#ifndef QTWASABI_SHOBJIDL_SHIM_H
#define QTWASABI_SHOBJIDL_SHIM_H

#include <windows.h>

#endif  // QTWASABI_SHOBJIDL_SHIM_H
