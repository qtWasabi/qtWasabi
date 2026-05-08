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

    // Fire the System.onScriptLoaded handler for every loaded script.
    // Returns the number of scripts where dispatch actually started
    // (i.e. their DLF table contains an "onScriptLoaded" entry AND
    // the bound SystemObject has a matching event entry).
    //
    // BIG WARNING (M13b): this WILL hit unimplemented stubs partway
    // through real script execution.  Several SOM / GuiObject / Group
    // method bodies still return defaults, and the opensourced VM hits
    // ASSERTs (or worse) once those defaults violate an invariant.
    // Run with WASABIQT_FATAL_ASSERTS=0 (the default) to log rather
    // than abort.  Stable use needs M13c — real method bodies for
    // setXmlParam / findObject / getAutoWidth / etc.
    int dispatchOnScriptLoaded();

    // After onScriptLoaded, fire System.onSetXuiParam(name, value)
    // for every non-standard attribute on a frame instantiation
    // (e.g. <Wasabi:MainFrame:NoStatus padtitleleft="10"/>).  Real
    // Wasabi delivers these to the embedded group's scripts at
    // instantiation; without them, titlebar.m's resizeObjects
    // never runs.  Broadcasts to every loaded script — scripts
    // that don't have an onSetXuiParam handler ignore the event.
    int dispatchXuiParams(const Layout::ResolvedWidget &root);

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
