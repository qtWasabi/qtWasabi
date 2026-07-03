#pragma once
// Port stub for a Windows-only Winamp header absent from public source
// mirrors.  Winamp's Main.h only declares GetRegistrar(IFileTypeRegistrar**,
// BOOL), so a forward declaration is all the Maki/pledit compile needs.
class IFileTypeRegistrar;
