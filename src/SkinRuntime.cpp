// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <qtWasabi/SkinRuntime.h>
#include <qtWasabi/SkinXml.h>
#include <qtWasabi/Layout.h>

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
namespace qtWasabi {
class BitmapRegistry;
void registerWidgetForScripts(const QString &id, Layout::ResolvedWidget *w,
                              void *scriptObjectHandle);
int  fireStaticTextChanged();
int  firePerObjectResize(void *skipObj);
void clearGeometryDirty();
bool geometryDirty();
void clearWidgetRegistry();
void setLayoutRootScriptObject(void *handle);
void setBitmapRegistry(BitmapRegistry *reg);
void setScriptOwnerWidget(int sid, void *scriptObjectHandle);
}

namespace qtWasabi {

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

    // Synthetic ScriptObject for the layout root, returned by the
    // bindings' getParentLayout().  See SkinRuntimeBridge.cpp for
    // the rationale — titlebar.m's resizeObjects centring math
    // depends on the layout's full w/h.
    void *layoutRootObject = nullptr;

    // Loaded VM script ids, in load order.
    QList<int> loadedScripts;

    // Script paths relative to skin root, for diagnostics.
    QStringList scriptPaths;

    // addScript stores a raw pointer into the blob we hand it
    // (codeBlock = p, no copy) so we have to keep every script's bytes
    // alive for the runtime's lifetime, otherwise the dispatcher reads
    // freed memory and walks off into opcode-table gaps.
    QList<QByteArray> scriptBlobs;

    // This runtime's window root (the layout-tree root passed to
    // loadScripts).  Identifies its per-window script context so dispatch
    // can switch the active root to it.  Stable across a skin reload (the
    // embedder reuses the same root object), so for the player it never
    // changes and the active-root switch is a no-op.
    const void *rootKey = nullptr;

    void destroyAll() {
        // loadedScripts may carry the same sid multiple times
        // when skin.xml referenced one (file, param) more than once.
        // Run the per-sid teardown only once each.
        QSet<int> uniqueIds;
        for (int id : loadedScripts) uniqueIds.insert(id);
        for (int id : uniqueIds) {
            Maki::registerScriptSystemObject(id, nullptr);
            Maki::registerScriptParam(id, nullptr);
            // Drop this runtime's owner-widget mapping by sid (g_scriptOwner
            // is shared across windows, so it must be torn down per-sid here
            // rather than globally in clearWidgetRegistry).
            setScriptOwnerWidget(id, nullptr);
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
        if (layoutRootObject) {
            Maki::destroyWidgetScriptObject(layoutRootObject);
            layoutRootObject = nullptr;
        }
        scriptPaths.clear();
        // Free script blobs only AFTER the VM has dropped them via
        // removeScript above, otherwise the codeBlock pointer in the
        // codeTable would dangle.
        scriptBlobs.clear();
        clearWidgetRegistry();
    }
};

SkinRuntime::SkinRuntime()  : m_d(new Impl) {}
SkinRuntime::~SkinRuntime() {
    // Tear down under THIS runtime's root so clearWidgetRegistry drops its
    // own window's entries, not whatever root happens to be active, then
    // forget the snapshot.  rootKey is null if it never loaded.
    if (m_d->rootKey) {
        ScopedScriptRoot guard(m_d->rootKey);
        m_d->destroyAll();
    } else {
        m_d->destroyAll();
    }
    const void *key = m_d->rootKey;
    delete m_d;
    if (key) dropScriptRoot(key);
}

void SkinRuntime::reset() {
    if (m_d->rootKey) {
        ScopedScriptRoot guard(m_d->rootKey);
        m_d->destroyAll();
    } else {
        m_d->destroyAll();
    }
}

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
    for (auto &c : w.children) if (c) registerWidgets(*c, out);
}

// Look for the first widget in the resolved tree that matches an
// owner-group id captured at parse time.  Matches in order of
// specificity:
//   1) exact widget id  (most common — the script is inside a
//      <group> instance the embedder named explicitly)
//   2) tag name  (the groupdef is instantiated as `<groupdef.id …/>`)
//   3) inherit_group attribute  (the instance referenced the groupdef
//      from a generic <group inherit_group="groupdef.id"/>)
// Returns nullptr if no match — caller then falls back to the layout
// root.
Layout::ResolvedWidget *findOwnerWidget(Layout::ResolvedWidget &root,
                                        const QString &gid) {
    if (gid.isEmpty()) return nullptr;
    Layout::ResolvedWidget *byId = nullptr;
    Layout::ResolvedWidget *byTag = nullptr;
    Layout::ResolvedWidget *byInherit = nullptr;
    Layout::ResolvedWidget *byExpansion = nullptr;
    std::function<void(Layout::ResolvedWidget &)> walk =
        [&](Layout::ResolvedWidget &w) {
        if (!byId       && w.id  == gid) byId = &w;
        if (!byTag      && w.tag == gid) byTag = &w;
        if (!byInherit  &&
            w.attrs.value(QStringLiteral("inherit_group")) == gid)
            byInherit = &w;
        // Expansion identity: groupdef instances (_srcgroupdef), XUI
        // instances (instanceId), and frame `content=` groupdefs whose
        // children were flattened into the frame node
        // (_content_groupdef) — a <script> declared inside any of these
        // owns the instantiating node.
        if (!byExpansion &&
            (w.instanceId == gid ||
             w.attrs.value(QStringLiteral("_srcgroupdef")) == gid ||
             w.attrs.value(QStringLiteral("_content_groupdef")) == gid))
            byExpansion = &w;
        for (auto &c : w.children) if (c) walk(*c);
    };
    walk(root);
    if (byId)        return byId;
    if (byTag)       return byTag;
    if (byInherit)   return byInherit;
    if (byExpansion) return byExpansion;
    return nullptr;
}
}  // namespace

void SkinRuntime::setBitmapRegistry(BitmapRegistry *reg) {
    qtWasabi::setBitmapRegistry(reg);
}

int SkinRuntime::loadScripts(const SkinXml::Document &doc,
                             Layout::ResolvedWidget &root) {
    // Activate THIS window's per-root script context before tearing down /
    // registering, so registration, the layout-root pseudo, and the bitmap
    // registry all land in this root's snapshot — not another window's.
    m_d->rootKey = &root;
    setActiveScriptRoot(&root);
    m_d->destroyAll();

    // 1) Build the widget-object table.
    registerWidgets(root, m_d->widgetObjects);

    // 1a) Bind a layout-root pseudo so getParentLayout() returns the
    // layout's full w/h (e.g. 354 for the player's normal layout)
    // rather than the calling widget's own bounds.
    m_d->layoutRootObject = Maki::createWidgetScriptObject(&root);
    setLayoutRootScriptObject(m_d->layoutRootObject);

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

    // Scope to THIS window: real Wasabi instantiates a container's own
    // scripts, not every <script> in the skin.  doc.scripts is document
    // -global, so without this filter a subwindow (Playlist Editor) also
    // loads the PLAYER's scripts (drawer, configtabs, …) and its resize
    // dispatch then fires their onResize with the SUBWINDOW's dimensions
    // — repositioning the player's drawer content for the wrong window
    // (the "opening/resizing the PL shifts the player's EQ drawer"
    // corruption).  A ref belongs to this window iff its owner scope
    // (closest enclosing groupdef/group/container/layout id) exists in
    // this root's tree; shared groupdefs (standardframe, menubar) match
    // in every window that instantiates them, so each still gets its own
    // per-window instance.  Owner-less refs stay document-global.
    {
        // Use the SAME owner resolution the script binding uses
        // (findOwnerWidget: id / tag / inherit_group) so a script loads
        // here exactly when it can bind here — a hand-rolled id walk
        // missed groupdefs instantiated through XUI `content=` params
        // (the pledit's own pledit.content.group) and dropped the
        // window's own scripts.
        QList<SkinXml::ScriptRef> scoped;
        for (const auto &r : refs) {
            const bool keep = r.ownerGroupId.isEmpty() ||
                              findOwnerWidget(root, r.ownerGroupId) != nullptr;
            if (::getenv("WASABIQT_TRACE_SCRIPTSCOPE"))
                std::fprintf(stderr, "[scriptscope] root=%p %s owner='%s' %s\n",
                             (void *)&root, r.file.toLocal8Bit().constData(),
                             r.ownerGroupId.toLocal8Bit().constData(),
                             keep ? "KEEP" : "skip");
            if (keep) scoped.append(r);
        }
        refs = scoped;
    }

    // Per-instance groupdef-body scripts.  A <script> inside a groupdef
    // is parsed ONCE (ownerGroupId = the groupdef), so a groupdef
    // instantiated N times shares a single script bound to one
    // instance.  Bento's InfoLine (one per file-info line) needs each
    // instance to run its OWN infoline.maki so each value positions
    // after its own label.  For any groupdef instantiated MORE THAN
    // ONCE, expand its script ref into one per instance (owner = the
    // instance's id).  Single-instance groupdefs are left exactly as
    // before — zero behavioural change for the rest of the chrome.
    {
        QList<SkinXml::ScriptRef> expanded;
        for (const auto &ref : refs) {
            QList<Layout::ResolvedWidget *> insts;
            if (!ref.ownerGroupId.isEmpty()) {
                std::function<void(Layout::ResolvedWidget &)> walk =
                    [&](Layout::ResolvedWidget &w) {
                        if (w.attrs.value(QStringLiteral("_srcgroupdef"))
                                == ref.ownerGroupId && !w.id.isEmpty())
                            insts.append(&w);
                        for (auto &c : w.children) if (c) walk(*c);
                    };
                walk(root);
            }
            if (insts.size() > 1) {
                for (auto *inst : insts) {
                    SkinXml::ScriptRef r = ref;
                    r.ownerGroupId = inst->id;   // findOwnerWidget keys on id
                    expanded.append(r);
                }
            } else {
                expanded.append(ref);
            }
        }
        refs = expanded;
    }

    // Stash params alive for the runtime's lifetime.
    m_d->scriptParams.clear();
    m_d->scriptParams.reserve(refs.size());

    // Dedupe identical (file, param) references so the same
    // script body + arg vector does not pay the addScript cost
    // multiple times. A common case is one .maki file referenced
    // twice in skin.xml under containers that happen to want the
    // exact same params. Refs that share the file path but differ
    // on param= still get distinct VM instances.
    QHash<QString, int> seen;     // "file|param|owner" -> sid
    for (const auto &ref : refs) {
        const QString &relPath = ref.file;
        // Include the owner in the dedup key so per-instance expansions
        // (same file+param, different instance owner) get DISTINCT VM
        // instances instead of collapsing to one.
        const QString key = relPath + QStringLiteral("|") + ref.param +
                            QStringLiteral("|") + ref.ownerGroupId;
        if (auto it = seen.constFind(key); it != seen.constEnd()) {
            const int sid = it.value();
            m_d->loadedScripts.append(sid);
            m_d->scriptPaths.append(relPath);
            // Keep parallel lists in shape but reference the same
            // backing entries we already created the first time.
            m_d->systemObjects.append(nullptr);   // null = duplicate
            m_d->scriptParams.push_back({});
            // The first instance already registered the owner.
            // Identical (file, param) means the owner-group hint is
            // also the same — leave the existing mapping alone.
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
        // reads it. Root cause of the id-collision bug: with predictedId
        // derived from scriptCount() (which never advanced because
        // addScript does not call assignNewScriptId itself), every
        // script collided on id=0 and getCodeBlock(0) returned the
        // wrong codeblock.
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

        // Predeclared globals (System, Config, …) reserve a variable
        // slot per script that the runtime would normally bind to the
        // right singleton.  Until per-class singleton plumbing lands,
        // hydrate every unbound object-typed slot with the script's
        // own SystemObject — System.foo() chains land on the actual
        // method bodies in maki-bindings.cpp, and unrelated singletons
        // (Config, Timer, …) at worst route through the same stubs
        // that already returned safe defaults via configDummy.
        Maki::hydrateNullObjectVars(sid, sysObj);
        // Diagnostic: which file landed at which sid.
        if (qEnvironmentVariableIntValue("WASABIQT_TRACE_SCRIPTS") == 1)
            qInfo().noquote() << QStringLiteral("[script] sid=%1 -> %2")
                                     .arg(sid).arg(relPath);

        m_d->loadedScripts.append(sid);
        m_d->scriptPaths.append(relPath);
        m_d->systemObjects.append(sysObj);

        // Resolve the parse-time ownerGroupId hint to a real widget
        // in the resolved tree and register it so wq_getScriptGroup
        // can hand it back during dispatch.
        if (!ref.ownerGroupId.isEmpty()) {
            if (Layout::ResolvedWidget *owner =
                    findOwnerWidget(root, ref.ownerGroupId)) {
                const QString &oid = !owner->id.isEmpty()
                                       ? owner->id
                                       : ref.ownerGroupId;
                void *handle = m_d->widgetObjects.value(oid, nullptr);
                if (!handle) {
                    // No bound handle (owner had no id).  Create a
                    // fresh handle so the script can call its methods.
                    handle = Maki::createWidgetScriptObject(owner);
                    if (!oid.isEmpty())
                        m_d->widgetObjects.insert(oid, handle);
                }
                setScriptOwnerWidget(sid, handle);
            }
        }

        // Register the per-script `param=` string with the bindings
        // layer.  Backed by an std::wstring we own for the runtime's
        // lifetime — the wchar_t* stays valid until destroyAll().
        m_d->scriptParams.push_back(ref.param.toStdWString());
        Maki::registerScriptParam(sid, m_d->scriptParams.back().c_str());

        // Remember this (file, param) so a later identical
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
    // Now that scripts have bound their per-widget handlers, replay
    // onTextChanged on statically-labelled text widgets so layout
    // handlers (InfoLine value positioning) run.
    fireStaticTextChanged();
    return fired;
}

void SkinRuntime::setPlayItemMetadataResolver(
        std::function<QString(const QString &)> resolver) {
    // Adapt the embedder's QString resolver to the wstring resolver the
    // Maki bindings call.  Empty function → bindings fall back to "".
    if (!resolver) {
        Maki::setPlayItemMetaResolver({});
        return;
    }
    Maki::setPlayItemMetaResolver(
        [resolver = std::move(resolver)](const std::wstring &key) -> std::wstring {
            const QString v = resolver(QString::fromStdWString(key));
            return v.toStdWString();
        });
}

int SkinRuntime::dispatchTitleChange(const QString &title) {
    const std::wstring w = title.toStdWString();
    int fired = 0;
    for (int sid : m_d->loadedScripts) {
        if (Maki::fireOnTitleChange(sid, w.c_str())) ++fired;
    }
    if (qEnvironmentVariableIntValue("WASABIQT_TRACE_META") == 1)
        qInfo("[meta] dispatchTitleChange('%s'): %d/%lld scripts handled onTitleChange",
              title.toLocal8Bit().constData(), fired,
              (long long)m_d->loadedScripts.size());
    return fired;
}

int SkinRuntime::dispatchPlaybackState(PlaybackState state) {
    const wchar_t *ev = nullptr;
    switch (state) {
        case PlaybackState::Playing: ev = L"onPlay";   break;
        case PlaybackState::Resumed: ev = L"onResume"; break;
        case PlaybackState::Paused:  ev = L"onPause";  break;
        case PlaybackState::Stopped: ev = L"onStop";   break;
    }
    if (!ev) return 0;
    int fired = 0;
    for (int sid : m_d->loadedScripts)
        if (Maki::fireSystemZeroArgEvent(sid, ev)) ++fired;
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
    // Frame instantiations carry XUI params (padtitleleft/right, content,
    // shade) that the embedded group's script (standardframe/titlebar.maki)
    // must receive via onSetXuiParam.  They're recognised by the
    // xuitag-aliased tag (wasabi_*frame_*) BEFORE layout expansion; AFTER
    // expansion the Expander rewrites the node's tag to "group" but stamps
    // the original groupdef id in `_srcgroupdef`, so also match a frame via
    // that stamp — otherwise padtitleright never reaches titlebar.maki and
    // the title streaks/buttons spacing is wrong.  General for any frame.
    const QString srcdef = w.attrs.value(QStringLiteral("_srcgroupdef"));
    const bool isFrame =
        (w.tag.startsWith(QStringLiteral("wasabi_")) &&
         w.tag.contains(QStringLiteral("frame"))) ||
        srcdef.contains(QStringLiteral("frame"));
    if (isFrame) {
        for (auto it = w.attrs.constBegin(); it != w.attrs.constEnd(); ++it) {
            if (isStandardAttr(it.key())) continue;
            // Forward only the titlebar-geometry XUI params.  `content`
            // and `shade` are handled by the static content-injection /
            // mousetrap paths; re-dispatching them through the VM would
            // double-inject the content group.  padtitleleft/right are
            // pure titlebar.maki resizeObjects inputs and safe to forward.
            const QString &k = it.key();
            if (k == QStringLiteral("padtitleleft") ||
                k == QStringLiteral("padtitleright")) {
                if (::getenv("WASABIQT_TRACE_XUI"))
                    std::fprintf(stderr, "[xui] frame id=%s srcdef=%s emit %s=%s\n",
                        w.id.toLocal8Bit().constData(), srcdef.toLocal8Bit().constData(),
                        k.toLocal8Bit().constData(), it.value().toLocal8Bit().constData());
                out.append({k, it.value()});
            }
        }
    }
    for (const auto &c : w.children) if (c) collectXuiParams(*c, out);
}
}  // namespace

int SkinRuntime::dispatchInitialResize(int layoutW, int layoutH) {
    if (!m_d->layoutRootObject) return 0;
    // Scope the whole cascade to THIS runtime's window: the per-object
    // pass below must only reach widgets registered under this root.
    // (A null rootKey — never loaded — re-selects the current root, a
    // no-op switch.)
    ScopedScriptRoot guard(m_d->rootKey ? m_d->rootKey : activeScriptRoot());
    int fired = 0;
    // Root-bound onResize handlers (e.g. configtabs) get the whole-layout
    // rect, as before.
    for (int sid : m_d->loadedScripts) {
        if (Maki::fireFourIntEvent(sid, m_d->layoutRootObject,
                                   L"onResize",
                                   0, 0, layoutW, layoutH))
            ++fired;
    }
    // Faithful per-GuiObject cascade, iterated to a GEOMETRY FIXPOINT.  Fire
    // onResize on every OTHER script-bound widget against its OWN resolved
    // client rect, so non-root handlers (playlist toolbar show/hide, footer
    // reflow) run — matching real Wasabi's per-object dispatch.  But a handler
    // may REFLOW the tree: pledit's g_playlist.onResize grows the playlist
    // column to full height, and only THEN (column tall) does playlistpro's
    // frameGroup.onResize reveal the search header + offset the list.  Real
    // Wasabi reacts to each geometry mutation by re-resolving and re-firing
    // onResize until the layout settles; we mirror that here.  After each
    // pass, if any geometry attr actually moved (g_geometryDirty), re-resolve
    // the whole tree (so lastCanvasRect — what the cascade reads — reflects
    // the new sizes) and run another pass.  Idempotent script guards
    // ("if(topbar.isVisible()) return;") converge it; bounded so a
    // pathological skin can't spin forever.  General: no skin-specific ids.
    void *ro = Maki::opaqueOf(m_d->layoutRootObject);
    auto *root = static_cast<Layout::ResolvedWidget *>(ro);
    const int kMaxSettle = 8;
    const bool traceSettle = ::getenv("WASABIQT_TRACE_SETTLE") != nullptr;
    int iter = 0;
    for (; iter < kMaxSettle; ++iter) {
        clearGeometryDirty();
        fired += firePerObjectResize(m_d->layoutRootObject);
        if (!geometryDirty()) { ++iter; break; }
        if (root) {
            const QSize canvas = root->lastCanvasRect.size();
            if (canvas.isValid() && !canvas.isEmpty())
                root->cacheResolvedRects(QPoint(0, 0), canvas);
        }
    }
    if (traceSettle)
        std::fprintf(stderr, "[settle] onResize fixpoint: %d passes%s\n",
                     iter, (iter >= kMaxSettle && geometryDirty())
                               ? " (HIT CAP, still dirty!)" : " (converged)");
    return fired;
}

int SkinRuntime::dispatchXuiParams(const Layout::ResolvedWidget &root) {
    QList<QPair<QString, QString>> params;
    collectXuiParams(root, params);
    int fired = 0;
    const bool trc = ::getenv("WASABIQT_TRACE_XUI") != nullptr;
    for (const auto &kv : params) {
        const std::wstring nm = kv.first.toStdWString();
        const std::wstring val = kv.second.toStdWString();
        for (int i = 0; i < m_d->loadedScripts.size(); ++i) {
            const int sid = m_d->loadedScripts[i];
            const bool ok = Maki::fireOnSetXuiParam(sid, nm.c_str(), val.c_str());
            if (trc && (ok || i < m_d->scriptPaths.size()))
                std::fprintf(stderr, "[xui] dispatch %s=%s -> sid=%d path=%s fired=%d\n",
                    kv.first.toLocal8Bit().constData(), kv.second.toLocal8Bit().constData(),
                    sid, i < m_d->scriptPaths.size() ? m_d->scriptPaths[i].toLocal8Bit().constData() : "?",
                    ok ? 1 : 0);
            if (ok) ++fired;
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

}  // namespace qtWasabi
