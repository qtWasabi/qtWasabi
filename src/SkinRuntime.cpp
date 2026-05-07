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
#include <QFileInfo>
#include <QHash>
#include <QString>

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

    // Loaded VM script ids, in load order.
    QList<int> loadedScripts;

    // Script paths relative to skin root, for diagnostics.
    QStringList scriptPaths;

    void destroyAll() {
        for (int id : loadedScripts) {
            Maki::registerScriptSystemObject(id, nullptr);
            Maki::removeScript(id);
        }
        loadedScripts.clear();
        for (void *h : systemObjects) Maki::destroyWidgetScriptObject(h);
        systemObjects.clear();
        for (void *h : widgetObjects) Maki::destroyWidgetScriptObject(h);
        widgetObjects.clear();
        scriptPaths.clear();
    }
};

SkinRuntime::SkinRuntime()  : m_d(new Impl) {}
SkinRuntime::~SkinRuntime() { m_d->destroyAll(); delete m_d; }

void SkinRuntime::reset() { m_d->destroyAll(); }

namespace {
void registerWidgets(Layout::ResolvedWidget &w,
                     QHash<QString, void *> &out) {
    if (!w.id.isEmpty() && !out.contains(w.id))
        out.insert(w.id, Maki::createWidgetScriptObject(&w));
    for (auto &c : w.children) registerWidgets(c, out);
}
}  // namespace

int SkinRuntime::loadScripts(const SkinXml::Document &doc,
                             Layout::ResolvedWidget &root) {
    m_d->destroyAll();

    // 1) Build the widget-object table.
    registerWidgets(root, m_d->widgetObjects);

    // 2) Load every <script file=…/> the skin references.
    int firedCount = 0;
    for (const auto &relPath : doc.scriptFiles) {
        const QString abs = QDir(doc.skinDir).filePath(relPath);
        QFile f(abs);
        if (!f.open(QIODevice::ReadOnly)) {
            qWarning() << "SkinRuntime: cannot open" << abs;
            continue;
        }
        const QByteArray blob = f.readAll();

        // Pre-allocate a SystemObject for this script (any
        // WidgetScriptObject suffices — the opensourced addScript only
        // needs vcpu_addAssignedVariable + getScriptObject to bind
        // it as var[0]).  We register it under the script id we'll
        // get back from addScript — but addScript returns the id, so
        // we need to register AFTER.  Workaround: we use the opensourced
        // VCPU::numScripts to predict the next id.
        void *sysObj = Maki::createWidgetScriptObject(nullptr);
        const int predictedId = Maki::scriptCount();    // next id
        Maki::registerScriptSystemObject(predictedId, sysObj);

        const int sid = Maki::addScript(blob.constData(), blob.size(), 0);
        if (sid < 0) {
            qWarning() << "SkinRuntime: addScript rejected" << relPath;
            Maki::registerScriptSystemObject(predictedId, nullptr);
            Maki::destroyWidgetScriptObject(sysObj);
            continue;
        }
        if (sid != predictedId) {
            // Fix up the registration if our prediction was off.
            Maki::registerScriptSystemObject(predictedId, nullptr);
            Maki::registerScriptSystemObject(sid, sysObj);
        }
        m_d->loadedScripts.append(sid);
        m_d->scriptPaths.append(relPath);
        m_d->systemObjects.append(sysObj);
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

int SkinRuntime::scriptCount() const {
    return m_d->loadedScripts.size();
}

int SkinRuntime::widgetObjectCount() const {
    return m_d->widgetObjects.size();
}

}  // namespace WasabiQt
