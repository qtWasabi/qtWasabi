// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// compat-version.cpp — minimal TU so CMake's OBJECT library has at
// least one source. It stamps the build with a version string so
// `nm` shows the lib is alive, alongside the real handle-registry /
// message-dispatcher / GDI sources.

namespace qtWasabi {
namespace wasabi_compat {

const char *compatLayerVersion() {
    return "qtwasabi_compat 0.1.0 (Phase A scaffolding)";
}

}  // namespace wasabi_compat
}  // namespace qtWasabi
