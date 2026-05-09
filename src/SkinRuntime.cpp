// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/SkinRuntime.h>
#include <WasabiQt/SkinXml.h>
#include <WasabiQt/Layout.h>

#include "../wasabi-port/maki-bridge.h"

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QSet>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QString>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// Bridge entry points implemented in src/SkinRuntimeBridge.cpp.
namespace WasabiQt {
void registerWidgetForScripts(const QString &id, Layout::ResolvedWidget *w,
                              void *scriptObjectHandle);
void clearWidgetRegistry();
}

namespace WasabiQt {

struct SkinRuntime::Impl {
    // Per-widget ScriptObject handles, keyed by widget id.  A handle
    // is created once per unique id so script-side identity holds
    // (`findObject(X) == findObject(X)`).
    QHash<QString, void *> widgetObjects;

    // Per-script SystemObject handles in load order.  Registered with
    // upstream's SOM::getSystemObjectByScriptId so addScript can bind
    // each as var[0] of its script.
    QList<void *> systemObjects;

    // Per-script `<script param="…">` strings.  std::wstring so the
    // wchar_t* exposed to the bindings layer stays valid until
    // destroyAll() runs.
    std::vector<std::wstring> scriptParams;

    // Loaded VM script ids, in load order.
    QList<int> loadedScripts;

    // Script paths relative to skin root, for diagnostics.
    QStringList scriptPaths;

    // M14a fix: addScript stores a raw pointer into the blob we hand it
    // (codeBlock = p, no copy) so we have to keep every script's bytes
    // alive for the runtime's lifetime, otherwise the dispatcher reads
    // freed memory and walks off into opcode-table gaps.
    QList<QByteArray> scriptBlobs;

    void destroyAll() {
        // M14h: loadedScripts may carry the same sid multiple times
        // when skin.xml referenced one (file, param) more than once.
        // Run the per-sid teardown only once each.
        QSet<int> uniqueIds;
        for (int id : loadedScripts) uniqueIds.insert(id);
        for (int id : uniqueIds) {
            Maki::registerScriptSystemObject(id, nullptr);
            Maki::registerScriptParam(id, nullptr);
            Maki::removeScript(id);
        }
        loadedScripts.clear();
        scriptParams.clear();
        for (void *h : systemObjects) {
            if (h) Maki::destroyWidgetScriptObject(h);
        }
        systemObjects.clear();
        for (void *h : widgetObjects) Maki::destroyWidgetScriptObject(h);
        widgetObjects.clear();
        scriptPaths.clear();
        // Free script blobs only AFTER the VM has dropped them via
        // removeScript above, otherwise the codeBlock pointer in the
        // codeTable would dangle.
        scriptBlobs.clear();
        clearWidgetRegistry();
    }
};

SkinRuntime::SkinRuntime()  : m_d(new Impl) {}
SkinRuntime::~SkinRuntime() { m_d->destroyAll(); delete m_d; }

void SkinRuntime::reset() { m_d->destroyAll(); }

namespace {
void registerWidgets(Layout::ResolvedWidget &w,
                     QHash<QString, void *> &out) {
    if (!w.id.isEmpty() && !out.contains(w.id)) {
        void *handle = Maki::createWidgetScriptObject(&w);
        out.insert(w.id, handle);
        // Make the handle reachable by id to the maki-bindings.cpp
        // accessors via the Qt-side bridge registry.
        registerWidgetForScripts(w.id, &w, handle);
    }
    for (auto &c : w.children) registerWidgets(c, out);
}
}  // namespace

int SkinRuntime::loadScripts(const SkinXml::Document &doc,
                             Layout::ResolvedWidget &root) {
    m_d->destroyAll();

    // 1) Build the widget-object table.
    registerWidgets(root, m_d->widgetObjects);

    // 2) Load every <script file=…/> the skin references.  Use the
    // ScriptRef list (carries both file + param) when present; fall
    // back to scriptFiles for backward compat.
    int firedCount = 0;
    QList<SkinXml::ScriptRef> refs = doc.scripts;
    if (refs.isEmpty()) {
        for (const auto &p : doc.scriptFiles) {
            SkinXml::ScriptRef r; r.file = p; refs.append(r);
        }
    }
    // Stash params alive for the runtime's lifetime.
    m_d->scriptParams.clear();
    m_d->scriptParams.reserve(refs.size());

    // M14h: dedupe identical (file, param) references so the same
    // script body + arg vector does not pay the addScript cost
    // multiple times. A common case is one .maki file referenced
    // twice in skin.xml under containers that happen to want the
    // exact same params. Refs that share the file path but differ
    // on param= still get distinct VM instances.
    QHash<QString, int> seen;     // "file|param" -> sid
    for (const auto &ref : refs) {
        const QString &relPath = ref.file;
        const QString key = relPath + QStringLiteral("|") + ref.param;
        if (auto it = seen.constFind(key); it != seen.constEnd()) {
            const int sid = it.value();
            m_d->loadedScripts.append(sid);
            m_d->scriptPaths.append(relPath);
            // Keep parallel lists in shape but reference the same
            // backing entries we already created the first time.
            m_d->systemObjects.append(nullptr);   // null = duplicate
            m_d->scriptParams.push_back({});
            continue;
        }
        const QString abs = QDir(doc.skinDir).filePath(relPath);
        QFile f(abs);
        if (!f.open(QIODevice::ReadOnly)) {
            qWarning() << "SkinRuntime: cannot open" << abs;
            continue;
        }
        // Retain the blob in the runtime, the VM will keep a raw
        // pointer into it. m_d->scriptBlobs is parallel to
        // loadedScripts so the lifetimes match.
        m_d->scriptBlobs.append(f.readAll());
        const QByteArray &blob = m_d->scriptBlobs.last();

        // Pre-allocate a SystemObject for this script (any
        // WidgetScriptObject suffices — the opensourced addScript only
        // needs vcpu_addAssignedVariable + getScriptObject to bind
        // it as var[0]).  We register it under the script id we'll
        // get back from addScript — but addScript returns the id, so
        // we need to register AFTER.  Workaround: we use the opensourced
        // VCPU::numScripts to predict the next id.
        void *sysObj = Maki::createWidgetScriptObject(nullptr);
        // Reserve a unique script id BEFORE addScript so each script
        // gets its own VM identity. assignNewScriptId() is what
        // actually increments VCPU::numScripts, scriptCount() merely
        // reads it. M14a root cause: with predictedId derived from
        // scriptCount() (which never advanced because addScript does
        // not call assignNewScriptId itself), every script collided
        // on id=0 and getCodeBlock(0) returned the wrong codeblock.
        const int predictedId = Maki::assignNewScriptId();
        Maki::registerScriptSystemObject(predictedId, sysObj);

        const int sid = Maki::addScript(blob.constData(), blob.size(),
                                        predictedId);
        if (sid < 0) {
            qWarning() << "SkinRuntime: addScript rejected" << relPath;
            Maki::registerScriptSystemObject(predictedId, nullptr);
            Maki::destroyWidgetScriptObject(sysObj);
            continue;
        }
        if (const char *tt = ::getenv("WASABIQT_TRACE_SCRIPTS");
            tt && *tt == '1') {
            QByteArray pn = relPath.toUtf8();
            std::fprintf(stderr, "[script-path] sid=%d %s\n", sid,
                         pn.constData());
        }
        if (sid != predictedId) {
            // Fix up the registration if our prediction was off.
            Maki::registerScriptSystemObject(predictedId, nullptr);
            Maki::registerScriptSystemObject(sid, sysObj);
        }

        // M14i: predeclared globals (Config, etc.) reserve a variable
        // slot per script that the runtime is supposed to bind to a
        // singleton. We do not have per-class singleton plumbing yet,
        // so hydrate every null SCRIPT_OBJECT slot with a shared
        // fallback. The Config method stubs in maki-bindings.cpp pick
        // it up so initAttribs() chains land cleanly.
        Maki::hydrateNullObjectVars(sid, Maki::getConfigDummy());
        // M14a diagnostic: which file landed at which sid.
        if (qEnvironmentVariableIntValue("WASABIQT_TRACE_SCRIPTS") == 1)
            qInfo().noquote() << QStringLiteral("[script] sid=%1 -> %2")
                                     .arg(sid).arg(relPath);

        m_d->loadedScripts.append(sid);
        m_d->scriptPaths.append(relPath);
        m_d->systemObjects.append(sysObj);

        // Register the per-script `param=` string with the bindings
        // layer.  Backed by an std::wstring we own for the runtime's
        // lifetime — the wchar_t* stays valid until destroyAll().
        m_d->scriptParams.push_back(ref.param.toStdWString());
        Maki::registerScriptParam(sid, m_d->scriptParams.back().c_str());

        // M14h: remember this (file, param) so a later identical
        // ref reuses the same sid.
        seen.insert(key, sid);
    }
    Q_UNUSED(firedCount);

    qInfo() << "SkinRuntime: loaded" << m_d->loadedScripts.size()
            << "scripts (dispatchOnScriptLoaded() to fire handlers)";
    return m_d->loadedScripts.size();
}

int SkinRuntime::dispatchOnScriptLoaded() {
    int fired = 0;
    for (int sid : m_d->loadedScripts) {
        const int dlfId = Maki::fireEventByName(sid, L"onScriptLoaded");
        if (dlfId >= 0) ++fired;
    }
    return fired;
}

namespace {
// Attributes that are part of the standard widget surface (geometry,
// id, visibility, etc.) — NOT XUI params delivered as events.
bool isStandardAttr(const QString &k) {
    static const QSet<QString> kStandard = {
        QStringLiteral("id"), QStringLiteral("instanceid"),
        QStringLiteral("xuitag"), QStringLiteral("embed_xui"),
        QStringLiteral("content"),
        QStringLiteral("x"), QStringLiteral("y"),
        QStringLiteral("w"), QStringLiteral("h"),
        QStringLiteral("relatx"), QStringLiteral("relaty"),
        QStringLiteral("relatw"), QStringLiteral("relath"),
        QStringLiteral("visible"), QStringLiteral("ghost"),
        QStringLiteral("alpha"), QStringLiteral("activealpha"),
        QStringLiteral("inactivealpha"),
        QStringLiteral("image"), QStringLiteral("downimage"),
        QStringLiteral("hoverimage"), QStringLiteral("activeimage"),
        QStringLiteral("font"), QStringLiteral("fontsize"),
        QStringLiteral("color"), QStringLiteral("align"),
        QStringLiteral("valign"), QStringLiteral("default"),
        QStringLiteral("display"), QStringLiteral("text"),
        QStringLiteral("forceuppercase"), QStringLiteral("bold"),
        QStringLiteral("italic"), QStringLiteral("antialias"),
        QStringLiteral("shadow"), QStringLiteral("shadowx"),
        QStringLiteral("shadowy"), QStringLiteral("shadowcolor"),
        QStringLiteral("tooltip"), QStringLiteral("action"),
        QStringLiteral("param"), QStringLiteral("rectrgn"),
        QStringLiteral("sysregion"), QStringLiteral("resize"),
        QStringLiteral("move"), QStringLiteral("file"),
        QStringLiteral("name"), QStringLiteral("gammagroup"),
        QStringLiteral("droptarget"), QStringLiteral("snapadjustbottom"),
        QStringLiteral("linkwidth"), QStringLiteral("minimum_w"),
        QStringLiteral("minimum_h"), QStringLiteral("default_x"),
        QStringLiteral("default_y"), QStringLiteral("default_w"),
        QStringLiteral("default_h"), QStringLiteral("default_visible"),
        QStringLiteral("nomenu"), QStringLiteral("autowidthsource"),
        QStringLiteral("charwidth"), QStringLiteral("charheight"),
        QStringLiteral("hspacing"), QStringLiteral("vspacing"),
        QStringLiteral("ticker"), QStringLiteral("rightclickaction"),
        QStringLiteral("dblclickaction"), QStringLiteral("dblclickAction"),
        QStringLiteral("leftpadding"), QStringLiteral("rightpadding"),
        QStringLiteral("showlen"), QStringLiteral("autoscroll"),
        QStringLiteral("activealpha2"), QStringLiteral("group"),
        QStringLiteral("target"),
    };
    return kStandard.contains(k);
}

void collectXuiParams(const Layout::ResolvedWidget &w,
                      QList<QPair<QString, QString>> &out) {
    // Frame instantiations are recognised by xuitag-aliased tags
    // (wasabi_*frame_*).  Every non-standard attr on them is an
    // XUI param that the embedded group's script should receive.
    if (w.tag.startsWith(QStringLiteral("wasabi_")) &&
        w.tag.contains(QStringLiteral("frame"))) {
        for (auto it = w.attrs.constBegin(); it != w.attrs.constEnd(); ++it) {
            if (!isStandardAttr(it.key()))
                out.append({it.key(), it.value()});
        }
    }
    for (const auto &c : w.children) collectXuiParams(c, out);
}
}  // namespace

int SkinRuntime::dispatchXuiParams(const Layout::ResolvedWidget &root) {
    QList<QPair<QString, QString>> params;
    collectXuiParams(root, params);
    int fired = 0;
    for (const auto &kv : params) {
        const std::wstring nm = kv.first.toStdWString();
        const std::wstring val = kv.second.toStdWString();
        for (int sid : m_d->loadedScripts) {
            if (Maki::fireOnSetXuiParam(sid, nm.c_str(), val.c_str()))
                ++fired;
        }
    }
    return fired;
}

int SkinRuntime::scriptCount() const {
    return m_d->loadedScripts.size();
}

int SkinRuntime::widgetObjectCount() const {
    return m_d->widgetObjects.size();
}

}  // namespace WasabiQt
