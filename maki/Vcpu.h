#pragma once
//
// Maki VM dispatch loop — port of Src/Wasabi/api/script/vcpu.cpp.
//
// Strategy: the VM understands every opcode that the shipped scripts
// emit, evaluates them on a tagged-variant stack, and routes CALLM
// to native bindings registered by GUID + function name (see
// Bindings.h).  The VM has zero knowledge of Wasabi-specific class
// identities — all native dispatch goes through the bindings table.

#include <WasabiQt/Maki.h>

#include <QHash>
#include <QString>
#include <QStack>
#include <QVariant>
#include <QVector>

#include <variant>

namespace wasabiq::maki {

class Bindings;       // Bindings.h
class IObject;        // Bindings.h

// Tagged variant for the operand stack and variable slots.  Mirrors
// the runtime shape of Maki's scriptVar without any C-level union
// tricks.  All numeric types collapse to double on arithmetic to
// match the upstream behaviour (OPCODE_ADD always returns
// SCRIPT_DOUBLE).
struct Slot {
    enum Kind { Null, Bool, Int, Double, String, Object };
    Kind kind = Null;
    qint64  i = 0;       // used by Int and Bool (0/1)
    double  d = 0.0;
    QString s;
    IObject *o = nullptr;

    int    varId = -1;   // -1 = literal; otherwise index into VM::m_vars
    int    typeTag = 0;  // type tag from the script's typeTable

    // Coerce the slot to a numeric double — used by ADD/SUB/MUL/...
    double asDouble() const {
        switch (kind) {
            case Bool:   return i ? 1.0 : 0.0;
            case Int:    return double(i);
            case Double: return d;
            case String: return s.toDouble();
            default:     return 0.0;
        }
    }
    qint64 asInt() const {
        switch (kind) {
            case Bool:   return i ? 1 : 0;
            case Int:    return i;
            case Double: return qint64(d);
            case String: return s.toLongLong();
            default:     return 0;
        }
    }
    bool asBool() const {
        switch (kind) {
            case Bool:   return i != 0;
            case Int:    return i != 0;
            case Double: return d != 0.0;
            case String: return !s.isEmpty();
            case Object: return o != nullptr;
            case Null:   return false;
        }
        return false;
    }
    bool isNumeric() const {
        return kind == Int || kind == Double || kind == Bool;
    }

    static Slot makeBool(bool v)         { Slot s; s.kind = Bool;   s.i = v ? 1 : 0;          return s; }
    static Slot makeInt(qint64 v)        { Slot s; s.kind = Int;    s.i = v;                  return s; }
    static Slot makeDouble(double v)     { Slot s; s.kind = Double; s.d = v;                  return s; }
    static Slot makeString(QString v)    { Slot s; s.kind = String; s.s = std::move(v);       return s; }
    static Slot makeObject(IObject *v)   { Slot s; s.kind = Object; s.o = v;                  return s; }
};

class VM {
public:
    VM();
    ~VM();

    void setBindings(Bindings *b) { m_bindings = b; }
    Bindings *bindings() const { return m_bindings; }

    // Register a parsed Script.  Returns its scriptId — used as the
    // first argument to dispatchEvent / setVariableValue.
    int  addScript(const Script &script);
    int  scriptCount() const { return m_scripts.size(); }

    // Set a script-side variable to a host-provided IObject*.  This
    // is how SkinEngine attaches the SystemObject (and similar) to
    // each script before any event fires.
    void setVariableObject(int scriptId, int variableId, IObject *o);
    void setVariableString(int scriptId, int variableId, const QString &s);

    // Look up an event hook for the given (objectVarId, dlfId) pair
    // and execute it.  Returns true if a handler ran.
    bool dispatchEvent(int scriptId, int variableId, int dlfId);

    // Dispatch every event hook bound to a given object across all
    // loaded scripts.  Used by SkinEngine to fire onPlay / onStop /
    // onResize etc. without callers knowing which scripts subscribe.
    bool dispatchEventByName(IObject *target, const QString &functionName);

    // Fire an event by name with operand-stack args.  Wasabi's
    // compiler stores handler arguments as variables at the top of
    // the script's variable table; the prologue at the handler's
    // codeOffset emits an OP_SET for each one to pop the stack into
    // those vars.  Push args before runFrom so the prologue finds
    // them in source order.
    bool dispatchEventByNameArgs(IObject *target, const QString &functionName,
                                 const QVector<Slot> &args);

    // Dispatch a named event for a specific script only — used to
    // fire onScriptLoaded right after a single script's bytecode
    // has been registered, without re-firing it on every previously
    // loaded script.
    bool dispatchEventInScript(int scriptId, IObject *target, const QString &functionName);

    // Number of opcodes executed across all dispatch calls.
    quint64 opcodesExecuted() const { return m_opcodesExecuted; }

    // Run a script's bytecode starting at a specific offset.  Used
    // for executing the static-initialisation block before any event
    // dispatches (init code lives at offset 0; event handlers start
    // at higher offsets).
    bool runFromOffset(int scriptId, int offset) {
        return runFrom(scriptId, offset, 0);
    }

    // Diagnostics — last guru-style error message.
    QString lastError() const { return m_lastError; }

    // Diagnostic — read a variable slot for inspection.
    Slot variableSlot(int scriptId, int variableId) const {
        if (scriptId < 0 || scriptId >= m_scripts.size()) return {};
        int idx = m_scripts[scriptId].varBase + variableId;
        if (idx < 0 || idx >= m_vars.size()) return {};
        return m_vars[idx];
    }

    // Per-script `<script param="…">` attribute string, queried by
    // System.getParam() via currentScriptId().  Bindings ask the VM
    // for the active script so they can look up the right param.
    void    setScriptParam(int scriptId, const QString &param) {
        if (scriptId < 0) return;
        if (scriptId >= m_scriptParams.size()) m_scriptParams.resize(scriptId + 1);
        m_scriptParams[scriptId] = param;
    }
    QString scriptParam(int scriptId) const {
        return scriptId >= 0 && scriptId < m_scriptParams.size()
            ? m_scriptParams[scriptId] : QString();
    }
    int     currentScriptId() const { return m_currentScript; }

private:
    struct Loaded {
        Script s;
        int    varBase = 0;     // index into VM::m_vars where this script's vars start
        int    dlfBase = 0;
    };

    bool runFrom(int scriptId, int codeOffset, int paramCount);

    Slot pop();
    void push(const Slot &v);

    Slot callDLF(const Loaded &l, int dlfId, int paramCount);
    void assignVar(int scriptId, int varId, const Slot &v);

    QList<Loaded>   m_scripts;
    QStack<Slot>    m_stack;
    QVector<Slot>   m_vars;     // flat per-script var storage
    QStack<int>     m_callStack;// return offsets for OP_CALLC

    Bindings       *m_bindings = nullptr;
    quint64         m_opcodesExecuted = 0;
    QString         m_lastError;
    QVector<QString> m_scriptParams;
    int             m_currentScript = -1;
};

} // namespace wasabiq::maki
