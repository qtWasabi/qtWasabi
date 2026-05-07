#pragma once
//
// StandardBindings — register the SystemObject + GuiObject method
// table on a Bindings instance.  Each script's first variable is
// the SystemObject; everything else (Group, Layer, Button, Slider)
// resolves to a RuntimeWidget that shares the GuiObject method
// surface.

#include "Bindings.h"
#include "RuntimeWidget.h"

namespace wasabiq {
class IWasabiHost;
}

namespace wasabiq::maki {

class VM;

// SystemObject — singleton lifetime; stored in VM's variable slot.
// Phase 3 implementation forwards to IWasabiHost.  Carries a
// back-pointer to the SkinEngine's root-scope facade so
// getScriptGroup() can return a wrapper scripts can use with
// findObject().
class SystemObject : public IObject {
public:
    SystemObject(wasabiq::IWasabiHost *host) : host(host) {}
    QUuid scriptObjectGuid() const override { return kSystemObjectGuid(); }
    wasabiq::IWasabiHost *host;
    wasabiq::SkinEngineRoot *skinRoot = nullptr;
    // The "global script group" wrapper that getScriptGroup hands out.
    // Owned by SkinEngine; lifetime = bind cycle.
    RuntimeWidget *globalGroup = nullptr;
    // Back-pointer to the VM so System.getParam() can ask which
    // script is currently running and look up its `<script param=>`.
    VM *vm = nullptr;
    // Per-script enclosing-group wrapper: indexed by script id.
    // Populated by SkinEngine::bindMakiHost when each script's
    // <script> tag's enclosing groupdef has a matching instance in
    // the layout.  Used by getScriptGroup binding.  Null entries
    // fall back to globalGroup.
    QVector<RuntimeWidget*> scriptGroups;
};

// Register all the standard methods (SystemObject, GuiObject, Group,
// Layer, Button, Slider, Text) on a Bindings instance.  Idempotent —
// safe to call multiple times.
void registerStandardBindings(Bindings &b);

} // namespace wasabiq::maki
