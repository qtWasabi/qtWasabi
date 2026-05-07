#pragma once
//
// Maki — Public surface of the bytecode VM.
//
// A Maki "program" is loaded from a `.maki` blob (the compiled output
// of mc.exe).  The loader produces a `Script` value containing the
// type table, DLF (Dynamic Link Function) table, variable table,
// strings, registered events, and the raw code bytes.  The VM
// (`MakiVM`) owns one or more loaded Scripts and executes their
// bytecode in response to events.
//
// Phase 2 scope: loader + scaffold for the VM. Full dispatch and
// native runtime bindings land in Phase 3.

#include <QByteArray>
#include <QList>
#include <QString>
#include <QUuid>
#include <QVariant>

namespace wasabiq::maki {

// One entry of the DLF table — names a callable function on a base
// class type identified by `basetype` (an index into the Script's
// type table after rebasing through CLASS_ID_BASE).
struct DLFEntry {
    int basetype = -1;            // resolved class index (or -1 if missing)
    int rawBasetype = -1;         // raw value as read from the file
    QString functionName;
};

// Per-script variable — initial value + flags.  Mirrors VCPUscriptVar
// from the opensourced source, but trimmed to what the loader needs.
struct ScriptVar {
    int   type = 0;               // class index (raw value as on disk)
    bool  transcient = false;
    bool  isStatic = false;
    bool  isaclass  = false;
    QVariant value;               // initial value (int / double / string / null-object)
};

// One <on EventName> hook in a script — runs starting at `codeOffset`
// when the engine's event dispatcher fires the matching variable's
// DLF entry.
struct EventHook {
    int variableId = -1;
    int dlfId      = -1;
    int codeOffset = 0;
};

// Header version.  Maki has shipped four versions in the wild — we
// support v3 (Winamp 5 era) and v4 (Modern). v1/v2 are rejected.
enum class HeaderVersion {
    Unknown = 0,
    V3      = 3,
    V4      = 4,
};

// A loaded .maki program — fully self-describing once parsed.
struct Script {
    HeaderVersion version = HeaderVersion::Unknown;
    QList<QUuid>      typeTable;     // class GUIDs
    QList<DLFEntry>   dlfTable;
    QList<ScriptVar>  variables;
    QList<EventHook>  events;
    QByteArray        code;          // raw bytecode bytes
};

// Parse a .maki blob.  Returns true if the file was recognised and
// fully parsed; false on any malformed structure.  errMsg, if
// non-null, receives a description of the first error.
bool loadScript(const QByteArray &blob, Script &out, QString *errMsg = nullptr);

// Forward declaration — the VM proper.  Implementation in Vcpu.cpp.
class VM;

} // namespace wasabiq::maki
