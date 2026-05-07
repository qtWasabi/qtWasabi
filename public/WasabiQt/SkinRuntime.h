#pragma once
//
// SkinRuntime — owns Maki script execution against a resolved widget
// tree.  M13 scaffold.  Each widget gets a `WidgetScriptObject*`
// (created via maki-bridge); each `<script file=…/>` reference in
// the skin XML gets loaded into the opensourced VCPU; per-script `param=`
// tokens drive the variable→widget binding.
//
// Foundation only at this stage — wires the data flow but doesn't
// yet make scripts visibly drive widget state.  Iterations:
//
//   M13a  load .maki blobs + bind WidgetScriptObjects                ← here
//   M13b  real SOM/ObjectTable bodies + run onScriptLoaded
//   M13c  setXmlParam / findObject / getAutoWidth wired into widgets
//   M13d  remove static `runKnownScripts` titlebar hack — let the
//         real titlebar.maki do it

#include <QtCore/qglobal.h>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace WasabiQt {

namespace SkinXml { struct Document; }
namespace Layout  { struct ResolvedWidget; }

class SkinRuntime {
public:
    SkinRuntime();
    ~SkinRuntime();

    // Walk `root`, create a WidgetScriptObject for every widget that
    // has an id, then load every `<script file=…/>` referenced in
    // `doc`.  Bindings run on first call and on every reload.
    int loadScripts(const SkinXml::Document &doc,
                    Layout::ResolvedWidget &root);

    // Number of scripts the VM currently holds.  Includes scripts
    // that loaded but had errors during onScriptLoaded.
    int scriptCount() const;

    // Number of widget objects bound — should be ≥ unique-id count
    // in the resolved tree.
    int widgetObjectCount() const;

    // Reset the VM to an empty state.  Discards all scripts +
    // widget objects (handles are deleted).
    void reset();

private:
    Q_DISABLE_COPY(SkinRuntime)
    struct Impl;
    Impl *m_d;
};

}  // namespace WasabiQt
