#include "Vcpu.h"
#include "Bindings.h"
#include "opcodes.h"

#include <QtEndian>
#include <QDebug>
#include <QScopeGuard>

namespace wasabiq::maki {

VM::VM()  = default;
VM::~VM() = default;

int VM::addScript(const Script &script) {
    Loaded l;
    l.s        = script;
    l.varBase  = m_vars.size();
    l.dlfBase  = 0;             // single-script process for now
    m_scripts.append(l);

    // Materialise initial variable slots.  Numeric / string literals
    // come from the on-disk variable + string tables (the loader
    // already copied string literals into ScriptVar::value).  Object
    // slots start as Null and get filled in by setVariableObject().
    for (const auto &sv : script.variables) {
        Slot slot;
        if (sv.value.isValid()) {
            switch (sv.value.metaType().id()) {
                case QMetaType::Int:
                case QMetaType::LongLong:
                    slot = Slot::makeInt(sv.value.toLongLong());
                    break;
                case QMetaType::Double:
                case QMetaType::Float:
                    slot = Slot::makeDouble(sv.value.toDouble());
                    break;
                case QMetaType::QString:
                    slot = Slot::makeString(sv.value.toString());
                    break;
                default:
                    break;
            }
        }
        slot.typeTag = sv.type;
        slot.varId = -1;   // initial values are literals, not variables
        m_vars.append(slot);
    }
    return m_scripts.size() - 1;
}

void VM::setVariableObject(int scriptId, int variableId, IObject *o) {
    if (scriptId < 0 || scriptId >= m_scripts.size()) return;
    const auto &l = m_scripts[scriptId];
    int idx = l.varBase + variableId;
    if (idx < 0 || idx >= m_vars.size()) return;
    Slot s = Slot::makeObject(o);
    s.varId = variableId;
    s.typeTag = l.s.variables[variableId].type;
    m_vars[idx] = s;
}

void VM::setVariableString(int scriptId, int variableId, const QString &str) {
    if (scriptId < 0 || scriptId >= m_scripts.size()) return;
    const auto &l = m_scripts[scriptId];
    int idx = l.varBase + variableId;
    if (idx < 0 || idx >= m_vars.size()) return;
    Slot s = Slot::makeString(str);
    s.varId = variableId;
    m_vars[idx] = s;
}

bool VM::dispatchEvent(int scriptId, int variableId, int dlfId) {
    if (scriptId < 0 || scriptId >= m_scripts.size()) return false;
    const auto &l = m_scripts[scriptId];
    for (const auto &h : l.s.events) {
        if (h.variableId == variableId && h.dlfId == dlfId) {
            return runFrom(scriptId, h.codeOffset, 0);
        }
    }
    return false;
}

bool VM::dispatchEventInScript(int scriptId, IObject *target, const QString &functionName)
{
    if (scriptId < 0 || scriptId >= m_scripts.size()) return false;
    const auto &l = m_scripts[scriptId];
    QString fnLower = functionName.toLower();
    bool any = false;
    for (const EventHook &h : l.s.events) {
        if (h.variableId < 0 || h.variableId >= l.s.variables.size()) continue;
        int vIdx = l.varBase + h.variableId;
        if (vIdx < 0 || vIdx >= m_vars.size()) continue;
        if (target && m_vars[vIdx].o != target) continue;
        if (h.dlfId < 0 || h.dlfId >= l.s.dlfTable.size()) continue;
        if (l.s.dlfTable[h.dlfId].functionName.compare(fnLower, Qt::CaseInsensitive) != 0)
            continue;
        if (runFrom(scriptId, h.codeOffset, 0)) any = true;
    }
    return any;
}

bool VM::dispatchEventByNameArgs(IObject *target, const QString &functionName,
                                 const QVector<Slot> &args)
{
    bool any = false;
    QString fnLower = functionName.toLower();
    bool trace = qEnvironmentVariableIsSet("WASABIQ_TRACE_DISPATCH");
    int candidates = 0;
    for (int scriptId = 0; scriptId < m_scripts.size(); ++scriptId) {
        const auto &l = m_scripts[scriptId];
        for (int hi = 0; hi < l.s.events.size(); ++hi) {
            const EventHook &h = l.s.events[hi];
            if (h.variableId < 0 || h.variableId >= l.s.variables.size()) continue;
            int vIdx = l.varBase + h.variableId;
            if (vIdx < 0 || vIdx >= m_vars.size()) continue;
            if (target && m_vars[vIdx].o != target) continue;
            if (h.dlfId < 0 || h.dlfId >= l.s.dlfTable.size()) continue;
            if (l.s.dlfTable[h.dlfId].functionName.compare(
                    fnLower, Qt::CaseInsensitive) != 0) continue;
            ++candidates;
            // Push args RIGHT-TO-LEFT — same convention as outgoing
            // CALLM (Wasabi compiler emits args in reverse so arg0
            // is on top of stack).  The handler entry bytecode pops
            // top → arg0 → assigns to first declared parameter,
            // pops next → arg1 → second parameter, and so on.
            // For onSetXuiParam(String param, String value):
            // push value first (deeper), push name last (top).  Pop
            // top → name → assigned to `param`.  Pop next → value
            // → assigned to `value`.  Result: handler sees
            // (param="padtitleright", value="25") in declared order.
            for (int i = args.size() - 1; i >= 0; --i) push(args[i]);
            if (runFrom(scriptId, h.codeOffset, args.size())) any = true;
        }
    }
    if (trace) {
        fprintf(stderr, "[wasabiq] dispatchArgs(%s) -> %d candidate(s), any=%d\n",
                qPrintable(functionName), candidates, int(any));
    }
    return any;
}

bool VM::dispatchEventByName(IObject *target, const QString &functionName) {
    bool any = false;
    QString fnLower = functionName.toLower();
    bool trace = qEnvironmentVariableIsSet("WASABIQ_TRACE");
    int matched = 0;
    for (int scriptId = 0; scriptId < m_scripts.size(); ++scriptId) {
        const auto &l = m_scripts[scriptId];
        for (int hi = 0; hi < l.s.events.size(); ++hi) {
            const EventHook &h = l.s.events[hi];
            if (h.variableId < 0 || h.variableId >= l.s.variables.size()) continue;
            int vIdx = l.varBase + h.variableId;
            if (vIdx < 0 || vIdx >= m_vars.size()) continue;
            // Match by object identity (or null target = global).
            if (target && m_vars[vIdx].o != target) continue;
            // Match by DLF function name.
            if (h.dlfId < 0 || h.dlfId >= l.s.dlfTable.size()) continue;
            if (l.s.dlfTable[h.dlfId].functionName.compare(
                    fnLower, Qt::CaseInsensitive) != 0) continue;
            matched++;
            if (runFrom(scriptId, h.codeOffset, 0)) any = true;
        }
    }
    if (trace && matched == 0 && functionName != "onScriptLoaded") {
        fprintf(stderr, "[wasabiq] dispatchEventByName(%s) no match\n",
                qPrintable(functionName));
    }
    return any;
}

// ── Helpers ────────────────────────────────────────────────────────

Slot VM::pop() {
    if (m_stack.isEmpty()) return Slot{};
    return m_stack.pop();
}
void VM::push(const Slot &v) { m_stack.push(v); }

void VM::assignVar(int scriptId, int varId, const Slot &v) {
    if (scriptId < 0 || scriptId >= m_scripts.size()) return;
    const auto &l = m_scripts[scriptId];
    int idx = l.varBase + varId;
    if (idx < 0 || idx >= m_vars.size()) return;
    Slot copy = v;
    copy.varId = varId;
    m_vars[idx] = copy;
}

Slot VM::callDLF(const Loaded &l, int dlfId, int paramCount) {
    if (dlfId < 0 || dlfId >= l.s.dlfTable.size()) return {};
    const DLFEntry &e = l.s.dlfTable[dlfId];

    // Resolve class GUID.  The .maki binary stores `basetype` either
    // as a raw class index (CLASS_ID_BASE..CLASS_ID_BASE+n) where the
    // index points into the script's type table, or as a higher
    // numeric value referring to an object variable's class.  We
    // mirror upstream's rebase: subtract CLASS_ID_BASE (0x100).
    constexpr int kClassIdBase = 0x100;
    QUuid classGuid;
    int rebased = e.rawBasetype - kClassIdBase;
    if (rebased >= 0 && rebased < l.s.typeTable.size()) {
        classGuid = l.s.typeTable[rebased];
    }

    // Look up the binding entry — both for dispatch AND so we can
    // figure out how many args to pop when the bytecode omits the
    // sentinel (paramCount == -1).
    const Bindings::Entry *be = m_bindings ? m_bindings->find(classGuid, e.functionName) : nullptr;

    // If the dispatched class GUID didn't have this method, the
    // self object's runtime GUID might.  Try that — but only AFTER
    // we've popped the args, since the result of a stale lookup
    // would still need to consume them.  We resolve `self` first.
    int np = paramCount;
    if (np < 0) np = be ? be->paramCount : 0;

    // Stack layout (Wasabi compiler emits right-to-left arg pushes):
    // bottom -> [..., self, arg(np-1), ..., arg1, arg0] <- top.
    // Empirical evidence: setXmlParam("x","152") came in as
    // {args[0]="152", args[1]="x"} with the previous left-to-right
    // assumption — i.e. arg0 was at the bottom, not the top.  Pop
    // top→args[0], next→args[1], … so the receiver gets the
    // declared (name, value) order.
    QVector<Slot> args(np);
    for (int i = 0; i < np; ++i) args[i] = pop();
    Slot self = pop();

    if (!be && self.o) {
        be = m_bindings ? m_bindings->find(self.o->scriptObjectGuid(), e.functionName) : nullptr;
    }
    if (!self.o) {
        // Calling a method on a null object — Wasabi treats this as
        // a guru meditation and returns 0.
        return Slot::makeInt(0);
    }
    if (!be) {
        m_lastError = QStringLiteral("CALLM: %1::%2 not bound")
            .arg(classGuid.toString(QUuid::WithoutBraces), e.functionName);
        if (qEnvironmentVariableIsSet("WASABIQ_TRACE")) {
            fprintf(stderr, "[wasabiq] %s\n", qPrintable(m_lastError));
        }
        return Slot::makeInt(0);
    }
    Slot result = be->fn(self.o, args);
    if (qEnvironmentVariableIsSet("WASABIQ_VTRACE")) {
        fprintf(stderr, "  CALL %s self=%p np=%d -> kind=%d o=%p\n",
                qPrintable(e.functionName), self.o, np,
                int(result.kind), result.o);
    }
    return result;
}

// ── The dispatch loop ──────────────────────────────────────────────

bool VM::runFrom(int scriptId, int codeOffset, int paramCount) {
    Q_UNUSED(paramCount);
    if (scriptId < 0 || scriptId >= m_scripts.size()) return false;
    const Loaded &l = m_scripts[scriptId];
    const QByteArray &code = l.s.code;
    if (codeOffset < 0 || codeOffset > code.size()) return false;

    int ip = codeOffset;
    int callBaseDepth = m_callStack.size();
    bool quit = false;
    int prevScript = m_currentScript;
    m_currentScript = scriptId;
    auto restoreCurrent = qScopeGuard([&] { m_currentScript = prevScript; });

    auto readI32 = [&]() -> qint32 {
        if (ip + 4 > code.size()) return 0;
        qint32 v = qFromLittleEndian<qint32>(code.constData() + ip);
        ip += 4;
        return v;
    };

    while (!quit && ip < code.size()) {
        quint8 op = static_cast<quint8>(code[ip++]);
        m_opcodesExecuted++;
        // Safety: stop runaway scripts (defensive bound — should
        // never trip after the CALLM stack-leak fix).
        if (m_stack.size() > 4096 || m_callStack.size() > 256) {
            fprintf(stderr, "[wasabiq] runaway script: stack=%d cs=%d ip=%d\n",
                    m_stack.size(), m_callStack.size(), ip - 1);
            return false;
        }
        switch (op) {

        case OP_NOP:
            break;

        case OP_PUSH: {
            qint32 varId = readI32();
            int idx = l.varBase + varId;
            if (idx >= 0 && idx < m_vars.size()) {
                Slot s = m_vars[idx];
                s.varId = varId;
                push(s);
            } else {
                push({});
            }
            break;
        }

        case OP_POPI:
            pop();
            break;

        case OP_POP: {
            qint32 varId = readI32();
            Slot v = pop();
            assignVar(scriptId, varId, v);
            break;
        }

        case OP_SET: {
            Slot v2 = pop();
            Slot v1 = pop();
            if (v1.varId < 0) {
                m_lastError = "OP_SET: lhs not a variable";
                return false;
            }
            assignVar(scriptId, v1.varId, v2);
            push(v2);
            break;
        }

        case OP_RET:
        case OP_RETF: {
            if (m_callStack.size() == callBaseDepth) { quit = true; break; }
            ip = m_callStack.pop();
            break;
        }

        case OP_CALLC: {
            qint32 shift = readI32();
            m_callStack.push(ip);
            ip += shift;
            break;
        }

        case OP_CALLM: {
            qint32 dlfId = readI32();
            // Peek the next 4 bytes — only consume them if they
            // form the upstream "stack protection" sentinel (high
            // 16 bits = 0xFFFF).  Otherwise leave them in the
            // bytecode stream; np stays -1.
            qint32 np = -1;
            if (ip + 4 <= code.size()) {
                quint32 peek = qFromLittleEndian<quint32>(code.constData() + ip);
                if ((peek & 0xFFFF0000u) == 0xFFFF0000u) {
                    np = peek & 0xFFFF;
                    ip += 4;
                }
            }
            Slot r = callDLF(l, dlfId, np);
            push(r);
            break;
        }
        case OP_CALLM2: {
            qint32 dlfId = readI32();
            if (ip >= code.size()) { quit = true; break; }
            quint8 np = static_cast<quint8>(code[ip++]);
            Slot r = callDLF(l, dlfId, np);
            push(r);
            break;
        }

        // ── Comparisons ────────────────────────────────────────────
        case OP_CMPEQ: {
            Slot v2 = pop(), v1 = pop();
            bool eq = false;
            if (v1.kind == Slot::String || v2.kind == Slot::String) {
                eq = (v1.s == v2.s);
            } else if (v1.kind == Slot::Object || v2.kind == Slot::Object) {
                eq = (v1.o == v2.o);
            } else {
                eq = qFuzzyCompare(v1.asDouble() + 1.0, v2.asDouble() + 1.0);
            }
            push(Slot::makeBool(eq));
            break;
        }
        case OP_CMPNE: {
            Slot v2 = pop(), v1 = pop();
            bool ne;
            if (v1.kind == Slot::String || v2.kind == Slot::String) ne = (v1.s != v2.s);
            else if (v1.kind == Slot::Object || v2.kind == Slot::Object) ne = (v1.o != v2.o);
            else ne = !qFuzzyCompare(v1.asDouble() + 1.0, v2.asDouble() + 1.0);
            push(Slot::makeBool(ne));
            break;
        }
        case OP_CMPA: {
            Slot v2 = pop(), v1 = pop();
            push(Slot::makeBool(v1.asDouble() >  v2.asDouble()));
            break;
        }
        case OP_CMPAE: {
            Slot v2 = pop(), v1 = pop();
            push(Slot::makeBool(v1.asDouble() >= v2.asDouble()));
            break;
        }
        case OP_CMPB: {
            Slot v2 = pop(), v1 = pop();
            push(Slot::makeBool(v1.asDouble() <  v2.asDouble()));
            break;
        }
        case OP_CMPBE: {
            Slot v2 = pop(), v1 = pop();
            push(Slot::makeBool(v1.asDouble() <= v2.asDouble()));
            break;
        }

        // ── Branches ───────────────────────────────────────────────
        case OP_JIZ: {
            qint32 shift = readI32();
            Slot v = pop();
            if (!v.asBool()) ip += shift;
            break;
        }
        case OP_JNZ: {
            qint32 shift = readI32();
            Slot v = pop();
            if (v.asBool()) ip += shift;
            break;
        }
        case OP_JMP: {
            qint32 shift = readI32();
            ip += shift;
            break;
        }

        // ── Arithmetic ─────────────────────────────────────────────
        case OP_ADD: {
            Slot v2 = pop(), v1 = pop();
            if (v2.kind == Slot::String || v1.kind == Slot::String) {
                QString concat;
                if (v1.kind == Slot::String) concat += v1.s;
                else                          concat += QString::number(v1.asDouble());
                if (v2.kind == Slot::String) concat += v2.s;
                else                          concat += QString::number(v2.asDouble());
                push(Slot::makeString(concat));
            } else {
                push(Slot::makeDouble(v1.asDouble() + v2.asDouble()));
            }
            break;
        }
        case OP_SUB: { Slot v2 = pop(), v1 = pop(); push(Slot::makeDouble(v1.asDouble() - v2.asDouble())); break; }
        case OP_MUL: { Slot v2 = pop(), v1 = pop(); push(Slot::makeDouble(v1.asDouble() * v2.asDouble())); break; }
        case OP_DIV: {
            Slot v2 = pop(), v1 = pop();
            double b = v2.asDouble();
            push(Slot::makeDouble(b == 0.0 ? 0.0 : v1.asDouble() / b));
            break;
        }
        case OP_MOD: {
            Slot v2 = pop(), v1 = pop();
            qint64 b = v2.asInt();
            push(Slot::makeInt(b == 0 ? 0 : v1.asInt() % b));
            break;
        }
        case OP_NEG: {
            Slot v = pop();
            if (v.kind == Slot::Int)        push(Slot::makeInt(-v.i));
            else if (v.kind == Slot::Double) push(Slot::makeDouble(-v.d));
            else                              push(v);
            break;
        }
        case OP_NOT: {
            Slot v = pop();
            push(Slot::makeBool(!v.asBool()));
            break;
        }
        case OP_BNOT: {
            Slot v = pop();
            push(Slot::makeInt(~v.asInt()));
            break;
        }

        // ── Bitwise / logical ──────────────────────────────────────
        case OP_AND: { Slot v2 = pop(), v1 = pop(); push(Slot::makeInt(v1.asInt() & v2.asInt())); break; }
        case OP_OR:  { Slot v2 = pop(), v1 = pop(); push(Slot::makeInt(v1.asInt() | v2.asInt())); break; }
        case OP_XOR: { Slot v2 = pop(), v1 = pop(); push(Slot::makeInt(v1.asInt() ^ v2.asInt())); break; }
        case OP_SHL: { Slot v2 = pop(), v1 = pop(); push(Slot::makeInt(v1.asInt() << v2.asInt())); break; }
        case OP_SHR: { Slot v2 = pop(), v1 = pop(); push(Slot::makeInt(v1.asInt() >> v2.asInt())); break; }
        case OP_LAND: { Slot v2 = pop(), v1 = pop(); push(Slot::makeBool(v1.asBool() && v2.asBool())); break; }
        case OP_LOR:  { Slot v2 = pop(), v1 = pop(); push(Slot::makeBool(v1.asBool() || v2.asBool())); break; }

        // ── In/decrement ──────────────────────────────────────────
        case OP_INCS: {
            Slot v = pop();
            push(v);          // post-incr returns old
            Slot nv = v;
            if (nv.kind == Slot::Int)     nv.i  += 1;
            else if (nv.kind == Slot::Double) nv.d += 1.0;
            else                           nv = Slot::makeInt(v.asInt() + 1);
            if (v.varId >= 0) assignVar(scriptId, v.varId, nv);
            break;
        }
        case OP_DECS: {
            Slot v = pop();
            push(v);
            Slot nv = v;
            if (nv.kind == Slot::Int)     nv.i  -= 1;
            else if (nv.kind == Slot::Double) nv.d -= 1.0;
            else                           nv = Slot::makeInt(v.asInt() - 1);
            if (v.varId >= 0) assignVar(scriptId, v.varId, nv);
            break;
        }
        case OP_INCP: {
            Slot v = pop();
            Slot nv = v;
            if (nv.kind == Slot::Int)     nv.i  += 1;
            else if (nv.kind == Slot::Double) nv.d += 1.0;
            else                           nv = Slot::makeInt(v.asInt() + 1);
            if (v.varId >= 0) assignVar(scriptId, v.varId, nv);
            push(nv);
            break;
        }
        case OP_DECP: {
            Slot v = pop();
            Slot nv = v;
            if (nv.kind == Slot::Int)     nv.i  -= 1;
            else if (nv.kind == Slot::Double) nv.d -= 1.0;
            else                           nv = Slot::makeInt(v.asInt() - 1);
            if (v.varId >= 0) assignVar(scriptId, v.varId, nv);
            push(nv);
            break;
        }

        // ── Object lifetime ───────────────────────────────────────
        case OP_NEW: {
            qint32 classId = readI32();
            Q_UNUSED(classId);
            // Phase 3+: instantiate via Bindings.  For now push null.
            push(Slot::makeObject(nullptr));
            break;
        }
        case OP_DELETE: {
            Slot v = pop();
            // We don't own the object lifecycles in this VM build —
            // hosts manage native objects.  Drop the reference and
            // push the original back (matches upstream semantics).
            push(v);
            break;
        }

        // ── Unknown-member dispatch (UMV/UMC) ─────────────────────
        case OP_UMV: {
            // pop name, pop obj; read u32 retType; bind a getter on
            // the object by name.  We don't yet have generic property
            // accessors so push null.
            (void)readI32();   // retType
            pop();             // name
            pop();             // object
            push({});
            break;
        }
        case OP_UMC: {
            // Unknown member call — same shape as CALLM but the
            // function is identified by name from the stack.  Skip.
            (void)readI32();
            push({});
            break;
        }

        case OP_CMPLT:
            // Upstream: "complete = 1; break;" — no-op.
            break;

        default:
            m_lastError = QStringLiteral("Maki VM: unhandled opcode 0x%1 at ip=%2")
                .arg(QString::number(op, 16), QString::number(ip - 1));
            fprintf(stderr, "[wasabiq] %s\n", qPrintable(m_lastError));
            return false;
        }
    }
    return true;
}

} // namespace wasabiq::maki
