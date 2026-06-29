// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once
//
// skin-service.h — WASABI_API_SKIN minimal implementation.
//
// gen_ml + ml_* query the skin service for:
//   * "is a modern skin loaded?" — controls whether the plugin
//     renders chrome itself or delegates to host skin widgets.
//   * Active skin name / path — passes to UI labels.
//   * Skin font / colour — used by SkinnedWnd's owner-draw paths.
//
// We don't want to take a hard link dependency on qtWasabi's
// `SkinRuntime` here — wasabi-compat sits BELOW the embedder
// stack so the SkinRuntime instance may not exist yet at compat
// init.  Instead we expose a setter so qtWasabi's bootstrap can
// inject a `SkinRuntime *` once it's ready; until then the
// queries return safe defaults.
//

#include "service-registry.h"

namespace qtWasabi {
class SkinRuntime;

namespace wasabi_compat {
namespace svc {

class SkinService : public ServiceObject {
public:
    GUID         guid()        const override { return SKIN_GUID; }
    const char  *typeName()    const override { return "skin"; }
    const char  *displayName() const override { return "qtWasabi Skin adapter"; }

    // Embedder hookups — qtamp (or whichever host) sets the
    // bound SkinRuntime once it's constructed.  Null means
    // "no skin yet"; queries return safe defaults.
    void setBoundRuntime(qtWasabi::SkinRuntime *rt) { m_rt = rt; }
    qtWasabi::SkinRuntime *boundRuntime() const     { return m_rt; }

    // Convenience predicates the plugin code branches on.
    bool isModernSkinLoaded() const { return m_rt != nullptr; }

    static SkinService &instance() {
        static SkinService s;
        return s;
    }

private:
    qtWasabi::SkinRuntime *m_rt = nullptr;
};

}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
