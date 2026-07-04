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

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <unordered_map>

namespace qtWasabi::Maki {

class WidgetScriptObject : public ScriptObject {
public:
    WidgetScriptObject() = default;
    ~WidgetScriptObject() override = default;

    void  setOpaque(void *w) { m_widget = w; }
    // Registry class index stamped at construction (ObjectTable::
    // instantiate for `new Foo`); -1 = classless (widget handles,
    // singletons).  Typed behaviour attaches here progressively.
    void  setClassIdx(int idx) { m_classIdx = idx; }
    int   classIdx() const { return m_classIdx; }
    void *opaque() const     { return m_widget; }
    void  setAttributeTag(const wchar_t *n) { m_isAttribute = true; m_attrName = n ? n : L""; }

    int   scriptId() const   { return m_scriptId; }
    const std::unordered_map<int, int> &assignedVariables() const { return m_assigned; }

    // ── opensourced-VM virtual hooks ──────────────────────────────────
    void  vcpu_setScriptId(int i) override            { m_scriptId = i; }
    void  vcpu_addAssignedVariable(int v, int s) override
                                                       {
        m_assigned[v] = s;
        if (std::getenv("WASABIQT_TRACE_ATTRIB"))
            std::fprintf(stderr, "[attrib] addAssignedVar this=%p var=%d sid=%d\n",
                         (void *)this, v, s);
    }
    void  vcpu_removeAssignedVariable(int v, int) override
                                                       { m_assigned.erase(v); }
    void  vcpu_delMembers(int scriptid) override {
        // Drop this script's member-var orphans (the orphan global
        // ids themselves are reclaimed by the VM reset()).
        for (auto it = m_members.begin(); it != m_members.end(); )
            it = (it->first.first == scriptid) ? m_members.erase(it)
                                               : std::next(it);
    }

    int   vcpu_getAssignedVariable(int start, int scriptid, int functionId,
                                   int *next, int *globalevententry,
                                   int * /*inheritedevent*/) override {
        // `start` is a global event-index hint — executeEvent calls us
        // in a loop, each iteration advancing through events.  Iterate
        // VCPU::eventsTable from `start`; for each entry, check if its
        // (varId, scriptId) matches one of our m_assigned pairs AND
        // its DLFid matches functionId.  Returns the event's varId
        // and sets *next to the index PAST the matched event so the
        // next loop iteration finds the next handler.
        //
        // This is the load-bearing piece that lets multiple handlers
        // for the same event (std.mi's onScriptLoaded + the user's
        // own) all fire instead of just the first.
        if (start < 0) start = 0;
        const int n = VCPU::eventsTable.getNumItems();
        for (int i = start; i < n; ++i) {
            VCPUeventEntry *ev = VCPU::eventsTable.enumItem(i);
            if (!ev) continue;
            if (ev->DLFid != functionId) continue;
            if (scriptid != -1 && ev->scriptId != scriptid) continue;
            // Does m_assigned hold this (varId, scriptId) pair?
            auto it = m_assigned.find(ev->varId);
            if (it == m_assigned.end() || it->second != ev->scriptId)
                continue;
            if (next) *next = i + 1;
            if (globalevententry) *globalevententry = i;
            if (m_isAttribute && std::getenv("WASABIQT_TRACE_ATTRIB"))
                std::fprintf(stderr,
                    "[attrib] getAssignedVar HIT this=%p name='%ls' start=%d qsid=%d fid=%d -> var=%d evsid=%d\n",
                    (void *)this, m_attrName.c_str(), start, scriptid, functionId, ev->varId, ev->scriptId);
            return ev->varId;
        }
        if (m_isAttribute && std::getenv("WASABIQT_TRACE_ATTRIB"))
            std::fprintf(stderr,
                "[attrib] getAssignedVar MISS this=%p name='%ls' start=%d qsid=%d fid=%d (m_assigned has %zu)\n",
                (void *)this, m_attrName.c_str(), start, scriptid, functionId, m_assigned.size());
        return -1;
    }

    void *vcpu_getInterfaceObject(GUID /*g*/, ScriptObject **o) override {
        if (o) *o = this;
        return this;
    }

    // Resolve a script-declared `Member` variable (e.g. eq.maki's
    // `Member int EqButton.setTo;`) to a real orphan lvalue, mirroring
    // upstream ScriptObjectI::vcpu_getMember.  The old hard `return -1`
    // made OPCODE_SET see an unassignable target and raise
    // GURU_SETNONINTERNAL (the recurring sid=29 fault).  We lazily
    // create one VCPU orphan per (scriptid, member-name) and cache its
    // global id so repeated reads return the same lvalue.
    int   vcpu_getMember(const wchar_t *id, int scriptid,
                         int rettype) override {
        if (!id) return -1;
        const auto key = std::make_pair(scriptid, std::wstring(id));
        auto it = m_members.find(key);
        if (it != m_members.end()) return it->second;
        const int gid = VCPU::createOrphan(rettype);
        m_members.emplace(key, gid);
        return gid;
    }

    ScriptObject *getScriptObject() override { return this; }

private:
    void *m_widget = nullptr;       // opaque ResolvedWidget*
    int   m_classIdx = -1;          // Maki class registry index
    bool  m_isAttribute = false;    // tagged by wq_newAttribute (trace only)
    std::wstring m_attrName;
    int   m_scriptId = -1;
    std::unordered_map<int, int> m_assigned;     // varId → scriptId
    // (scriptId, member-name) → orphan global id, for vcpu_getMember.
    std::map<std::pair<int, std::wstring>, int> m_members;
};

// ── public bridge surface ────────────────────────────────────────

void *createWidgetScriptObject(void *opaqueWidget) {
    auto *obj = new WidgetScriptObject();
    obj->setOpaque(opaqueWidget);
    return obj;
}

void setScriptObjectClass(void *handle, int classIdx) {
    if (handle)
        static_cast<WidgetScriptObject *>(handle)->setClassIdx(classIdx);
}

int scriptObjectClassIdx(void *handle) {
    return handle
        ? static_cast<WidgetScriptObject *>(handle)->classIdx() : -1;
}

void tagScriptObjectAsAttribute(void *handle, const wchar_t *name) {
    if (handle)
        static_cast<WidgetScriptObject *>(handle)->setAttributeTag(name);
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

}  // namespace qtWasabi::Maki
