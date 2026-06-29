// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#ifndef QTWASABI_PLEDIT_HOST_RENDERER_H
#define QTWASABI_PLEDIT_HOST_RENDERER_H
namespace qtWasabi {
// Register the real-draw_pe playlist renderer for the Playlist GUID
// {45F3F7C1-…}, retiring the PlaylistPro substitute.  Call once at boot
// (src/main.cpp, beside installMlHostFactory()).
void installPleditHostFactory();
}
#endif
