#pragma once
//
// RuntimeWidget — IObject wrapper around a Wasabi::Widget*.  Stable
// across the lifetime of a loaded skin; each script-side `Group g;`
// or `Layer l;` ends up pointing at one.
//
// The wrapper holds a back-pointer to its parsed widget plus a
// pointer to the SkinEngine's runtime-state table, so binding
// implementations can read the immutable XML attributes AND mutate
// runtime overrides through one object.

#include "Bindings.h"
#include <wasabiq/WasabiLayout.h>

#include <QUuid>

namespace wasabiq {
class SkinRuntime;
}

namespace wasabiq::maki {

// Common GUIDs (lifted from scriptguid.h).  All the Wasabi widget
// classes share `guiObjectGuid` as their bindings entry-point —
// scripts mostly call methods on the GuiObject base.
inline const QUuid &kGuiObjectGuid() {
    static QUuid u("{4ee3e199-c636-4bec-97cd-78bc9c8628b0}");
    return u;
}
inline const QUuid &kGroupGuid() {
    static QUuid u("{45be95e5-2072-4191-935c-bb5ff9f117fd}");
    return u;
}
inline const QUuid &kLayerGuid() {
    // wasabi.layer (xmlObjectGuid) = FB508805-53D0-4f74-A62D-A1A8EF2B242C
    static QUuid u("{fb508805-53d0-4f74-a62d-a1a8ef2b242c}");
    return u;
}
inline const QUuid &kButtonGuid() {
    static QUuid u("{698eddcd-8f1e-4fec-9b12-f944f909ff45}");
    return u;
}
inline const QUuid &kToggleButtonGuid() {
    static QUuid u("{b4dccfff-81fe-4bcc-961b-720fd5be0fff}");
    return u;
}
inline const QUuid &kVisGuid() {
    // Vis class GUID from std.mi
    static QUuid u("{ce4f97be-77b0-4e19-9956-d49833c96c27}");
    return u;
}
inline const QUuid &kContainerGuid() {
    static QUuid u("{e90dc47b-840d-4ae7-b02c-040bd275f7fc}");
    return u;
}
inline const QUuid &kLayoutGuid() {
    static QUuid u("{60906d4e-537e-482e-b004-cc9461885672}");
    return u;
}
inline const QUuid &kSliderGuid() {
    static QUuid u("{62b65e3f-375e-408d-8dea-76814ab91b77}");
    return u;
}
inline const QUuid &kTextGuid() {
    static QUuid u("{efaa8672-310e-41fa-b7dc-85a9525bcb4b}");
    return u;
}
inline const QUuid &kSystemObjectGuid() {
    static QUuid u("{d6f50f64-93fa-49b7-93f1-ba66efae3e98}");
    return u;
}

// Forward decl — SkinEngine owns the wrapper pool and tree.
} // namespace wasabiq::maki
namespace wasabiq { class SkinEngineRoot; }
namespace wasabiq::maki {

class RuntimeWidget : public IObject {
public:
    RuntimeWidget(Wasabi::Widget *w, wasabiq::SkinRuntime *r)
        : widget(w), runtime(r) {}

    QUuid scriptObjectGuid() const override;

    Wasabi::Widget       *widget = nullptr;
    wasabiq::SkinRuntime *runtime = nullptr;

    // When non-null, findObject() recurses into the SkinEngine's
    // entire tree rather than just `widget->children`.  Used for the
    // "script scope" pseudo-group returned by getScriptGroup so
    // scripts without param= bindings can still find widgets.
    wasabiq::SkinEngineRoot *root = nullptr;
};

} // namespace wasabiq::maki
