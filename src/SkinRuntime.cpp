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

    // Loaded VM script ids, in load order.
    QList<int> loadedScripts;

    // Script paths relative to skin root, for diagnostics.
    QStringList scriptPaths;

    void destroyAll() {
        for (int id : loadedScripts) Maki::removeScript(id);
        loadedScripts.clear();
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
    for (const auto &relPath : doc.scriptFiles) {
        const QString abs = QDir(doc.skinDir).filePath(relPath);
        QFile f(abs);
        if (!f.open(QIODevice::ReadOnly)) {
            qWarning() << "SkinRuntime: cannot open" << abs;
            continue;
        }
        const QByteArray blob = f.readAll();
        const int sid = Maki::addScript(blob.constData(), blob.size(), 0);
        if (sid < 0) {
            qWarning() << "SkinRuntime: addScript rejected" << relPath;
            continue;
        }
        m_d->loadedScripts.append(sid);
        m_d->scriptPaths.append(relPath);
    }

    return m_d->loadedScripts.size();
}

int SkinRuntime::scriptCount() const {
    return m_d->loadedScripts.size();
}

int SkinRuntime::widgetObjectCount() const {
    return m_d->widgetObjects.size();
}

}  // namespace WasabiQt
