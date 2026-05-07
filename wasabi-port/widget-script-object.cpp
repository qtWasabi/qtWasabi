// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// widget-script-object.cpp — bridge between the opensourced ScriptObject
// vtable and a Qt-side ResolvedWidget*.  Sole TU that includes
// <api/script/scriptobj.h>; downstream code holds opaque
// WidgetScriptObject* handles via maki-bridge.h.
//
// The opensourced Maki VM dispatches every script-callable method
// (`setVisible`, `setXmlParam`, `getAutoWidth`, `findObject`, …) by
// looking up (classGuid, funcName) in ObjectTable.  Each instance
// also implements virtual methods (`vcpu_setScriptId`,
// `vcpu_addAssignedVariable`, …) that the VM calls during
// load/dispatch.  WidgetScriptObject overrides those so the VM can
// actually run scripts against our resolved widget tree without
// crashing.

#include <api/script/scriptobj.h>
#include <api/script/vcpu.h>
#include "maki-bridge.h"

// linux.h's min/max macros stomp on STL.  Pop them before pulling
// any standard-library header.
#ifdef min
#  undef min
#endif
#ifdef max
#  undef max
#endif

#include <unordered_map>

namespace WasabiQt::Maki {

class WidgetScriptObject : public ScriptObject {
public:
    WidgetScriptObject() = default;
    ~WidgetScriptObject() override = default;

    void  setOpaque(void *w) { m_widget = w; }
    void *opaque() const     { return m_widget; }

    int   scriptId() const   { return m_scriptId; }
    const std::unordered_map<int, int> &assignedVariables() const { return m_assigned; }

    // ── opensourced-VM virtual hooks ──────────────────────────────────
    void  vcpu_setScriptId(int i) override            { m_scriptId = i; }
    void  vcpu_addAssignedVariable(int v, int s) override
                                                       { m_assigned[v] = s; }
    void  vcpu_removeAssignedVariable(int v, int) override
                                                       { m_assigned.erase(v); }
    void  vcpu_delMembers(int) override                {}

    int   vcpu_getAssignedVariable(int start, int scriptid, int functionId,
                                   int *next, int *globalevententry,
                                   int * /*inheritedevent*/) override {
        // Walk our assigned variables; for each, walk VCPU::eventsTable
        // and find the entry matching (varId, scriptId, functionId).
        // Returns the varId of the first match.  Mirrors upstream
        // ScriptObjectI::vcpu_getAssignedVariable / getEventForVar.
        if (start < 0) start = 0;
        int idx = 0;
        for (const auto &pair : m_assigned) {
            if (idx < start) { ++idx; continue; }
            const int varId   = pair.first;
            const int varSid  = pair.second;
            if (scriptid != -1 && varSid != scriptid) { ++idx; continue; }
            const int n = VCPU::eventsTable.getNumItems();
            for (int i = 0; i < n; ++i) {
                VCPUeventEntry *ev = VCPU::eventsTable.enumItem(i);
                if (!ev) continue;
                if (ev->varId    != varId)  continue;
                if (ev->scriptId != varSid) continue;
                if (ev->DLFid    != functionId) continue;
                if (next) *next = idx + 1;
                if (globalevententry) *globalevententry = i;
                return varId;
            }
            ++idx;
        }
        return -1;
    }

    void *vcpu_getInterfaceObject(GUID /*g*/, ScriptObject **o) override {
        if (o) *o = this;
        return this;
    }

    int   vcpu_getMember(const wchar_t * /*id*/, int /*scriptid*/,
                         int /*rettype*/) override { return -1; }

    ScriptObject *getScriptObject() override { return this; }

private:
    void *m_widget = nullptr;       // opaque ResolvedWidget*
    int   m_scriptId = -1;
    std::unordered_map<int, int> m_assigned;     // varId → scriptId
};

// ── public bridge surface ────────────────────────────────────────

void *createWidgetScriptObject(void *opaqueWidget) {
    auto *obj = new WidgetScriptObject();
    obj->setOpaque(opaqueWidget);
    return obj;
}

void destroyWidgetScriptObject(void *handle) {
    delete static_cast<WidgetScriptObject *>(handle);
}

void *opaqueOf(void *handle) {
    return static_cast<WidgetScriptObject *>(handle)->opaque();
}

int scriptIdOf(void *handle) {
    return static_cast<WidgetScriptObject *>(handle)->scriptId();
}

}  // namespace WasabiQt::Maki
