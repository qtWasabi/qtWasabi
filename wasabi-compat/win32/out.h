// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// out.h — stub for Winamp's output-plugin SDK header (not present in the
// source drop).  in_wv/in2.h #includes it; Winamp/Main.h pulls in2.h, and
// the in-player playlist render path (draw_pe.cpp) pulls Main.h.  draw_pe
// uses none of the output-plugin API — only the Out_Module struct shape
// needs to exist so the include resolves.

#ifndef QTWASABI_OUT_H_SHIM
#define QTWASABI_OUT_H_SHIM

typedef struct {
    int   version;
    char *description;
    int   id;
    void *hMainWindow;
    void *hDllInstance;
    void (*Config)(void *hwndParent);
    void (*About)(void *hwndParent);
    void (*Init)();
    void (*Quit)();
    int  (*Open)(int samplerate, int numchannels, int bitspersamp,
                 int bufferlenms, int prebufferms);
    void (*Close)();
    int  (*Write)(char *buf, int len);
    int  (*CanWrite)();
    int  (*IsPlaying)();
    int  (*Pause)(int pause);
    void (*SetVolume)(int volume);
    void (*SetPan)(int pan);
    void (*Flush)(int t);
    int  (*GetOutputTime)();
    int  (*GetWrittenTime)();
} Out_Module;

#endif  // QTWASABI_OUT_H_SHIM
