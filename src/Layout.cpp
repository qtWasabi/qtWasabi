// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/Layout.h>
#include <WasabiQt/SkinXml.h>
#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/LayerPainter.h>

#include <QBitmap>
#include <QFont>
#include <QFontMetrics>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QRgb>
#include <QSet>
#include <QString>
#include <functional>

namespace WasabiQt::Layout {

namespace {

using SkinXml::Element;

// (instanceId, targetWidgetId) -> {attr: value}
using SendparamsMap = QHash<QPair<QString, QString>, QHash<QString, QString>>;

// Index groupdefs by their `id` AND by their `xuitag` (lowercased,
// colons replaced with underscores to match the normalisation that
// SkinXml::parse applies to element names).  Same node can appear
// under both keys.
struct GroupdefIndex {
    QHash<QString, const Element *> byId;
    QHash<QString, const Element *> byXuitag;

    const Element *lookup(const QString &key) const {
        if (auto *p = byId.value(key, nullptr))      return p;
        if (auto *p = byXuitag.value(key, nullptr))  return p;
        return nullptr;
    }
};

void collectGroupdefs(const Element &el, GroupdefIndex &out) {
    if (el.tag == QStringLiteral("groupdef")) {
        const QString id = el.attrs.value(QStringLiteral("id"));
        if (!id.isEmpty()) out.byId.insert(id, &el);
        const QString xui = el.attrs.value(QStringLiteral("xuitag"));
        if (!xui.isEmpty()) {
            // Match SkinXml's tag normalisation: lowercase, ':' → '_'.
            QString key = xui.toLower();
            key.replace(QChar(':'), QChar('_'));
            out.byXuitag.insert(key, &el);
        }
    }
    for (const auto &c : el.children) collectGroupdefs(c, out);
}

// `<hideobject target="id1;id2;…"/>` — explicit visibility override
// applied at expansion time.  Stored as a flat set of widget ids
// that should resolve to `visible="0"`.
//
// Skip hideobjects nested inside `<groupdef>` bodies: those are
// scoped to that groupdef's instantiation (e.g. wasabi.standardframe
// .modal hides MainMenu only when Modal is the active frame), and
// pulling them globally hides MainMenu in every layout — including
// the player's, which uses MainFrame:NoStatus, not Modal.
void collectHideObjects(const Element &el, QSet<QString> &out) {
    if (el.tag == QStringLiteral("groupdef")) return;
    if (el.tag == QStringLiteral("hideobject")) {
        const QString target = el.attrs.value(QStringLiteral("target"));
        for (const QString &t : target.split(QChar(';'), Qt::SkipEmptyParts))
            out.insert(t.trimmed());
    }
    for (const auto &c : el.children) collectHideObjects(c, out);
}

void collectSendparams(const Element &el, SendparamsMap &out) {
    if (el.tag == QStringLiteral("sendparams")) {
        const QString grp    = el.attrs.value(QStringLiteral("group"));
        const QString target = el.attrs.value(QStringLiteral("target"));
        // `target` can be a `;`-separated list — apply to each.
        if (!target.isEmpty()) {
            QHash<QString, QString> overrides;
            for (auto it = el.attrs.constBegin(); it != el.attrs.constEnd(); ++it) {
                const QString &k = it.key();
                if (k == QStringLiteral("group") || k == QStringLiteral("target"))
                    continue;
                overrides.insert(k, it.value());
            }
            for (const QString &t : target.split(QChar(';'),
                                                 Qt::SkipEmptyParts)) {
                auto &existing = out[{grp, t.trimmed()}];
                for (auto it = overrides.constBegin();
                     it != overrides.constEnd(); ++it)
                    existing.insert(it.key(), it.value());
            }
        }
    }
    for (const auto &c : el.children) collectSendparams(c, out);
}

const Element *findContainer(const Element &root, const QString &id) {
    if (root.tag == QStringLiteral("container") &&
        root.attrs.value(QStringLiteral("id"))
            .compare(id, Qt::CaseInsensitive) == 0)
        return &root;
    for (const auto &c : root.children) {
        if (auto *r = findContainer(c, id)) return r;
    }
    return nullptr;
}

const Element *findLayout(const Element &container, const QString &id) {
    for (const auto &c : container.children) {
        if (c.tag == QStringLiteral("layout") &&
            c.attrs.value(QStringLiteral("id")) == id)
            return &c;
    }
    return nullptr;
}

ResolvedWidget makeResolved(const Element &src) {
    ResolvedWidget r;
    r.tag        = src.tag;
    r.id         = src.attrs.value(QStringLiteral("id"));
    r.instanceId = src.attrs.value(QStringLiteral("instanceid"));
    r.attrs      = src.attrs;
    r.sourceFile = src.sourceFile;
    r.sourceLine = src.sourceLine;
    return r;
}

// ──────────────────────────────────────────────────────────────────
// The expansion walker.  `expand(src, out, currentInstanceId)`
// produces children of `src` into `out.children`, expanding any
// `<group id="…">` whose id matches a known groupdef.
//
// `currentInstanceId` is the instanceid of the nearest enclosing
// group instance — sendparams targeting widgets within that
// instance are applied here.

class Expander {
public:
    Expander(const GroupdefIndex &groupdefs,
             const SendparamsMap &sendparams,
             const QSet<QString> &hidden)
        : m_groupdefs(groupdefs), m_sendparams(sendparams),
          m_hidden(hidden) {}

    void expandChildren(const Element &src, ResolvedWidget &out,
                        const QString &instanceId) {
        for (const auto &child : src.children)
            visit(child, out, instanceId);
    }

private:
    static bool isScaffolding(const QString &tag) {
        return tag == QStringLiteral("sendparams")     ||
               tag == QStringLiteral("groupdef")       ||
               tag == QStringLiteral("scripts")        ||
               tag == QStringLiteral("script")         ||
               tag == QStringLiteral("elementalias")   ||
               tag == QStringLiteral("hideobject")     ||
               tag == QStringLiteral("color")          ||
               tag == QStringLiteral("gammagroup")     ||
               tag == QStringLiteral("gammaset")       ||
               tag == QStringLiteral("bitmap")         ||
               tag == QStringLiteral("bitmapfont")     ||
               tag == QStringLiteral("accelerators")   ||
               tag == QStringLiteral("accelerator")    ||
               // colorthemes_list is the <ColorThemes:List> widget,
               // a real renderable list of available gammasets —
               // NOT scaffolding.  Only the runtime-only manager
               // (colorthemes_mgr) stays a no-op here.
               tag == QStringLiteral("colorthemes_mgr") ||
               tag == QStringLiteral("skininfo");
    }

    void visit(const Element &el, ResolvedWidget &parent,
               const QString &instanceId) {
        if (isScaffolding(el.tag)) return;

        // Resolve groupdef references.  Two ways an element can match:
        //   • it's `<group id="X">` and `X` is a known groupdef id
        //   • its tag itself is a known xuitag alias
        //     (`<wasabi_mainframe_nostatus>` → groupdef
        //      `wasabi.mainframe.nostatusbar` whose
        //      `xuitag="Wasabi:MainFrame:NoStatus"`)
        const Element *def = nullptr;
        if (el.tag == QStringLiteral("group")) {
            def = m_groupdefs.lookup(el.attrs.value(QStringLiteral("id")));
        } else {
            def = m_groupdefs.byXuitag.value(el.tag, nullptr);
        }

        if (def) {
            const QString gid = def->attrs.value(QStringLiteral("id"));
            const QString iid = el.attrs.value(QStringLiteral("instanceid"));
            // Detect direct cycles.
            if (m_inflightInstances.contains(gid)) return;
            m_inflightInstances.insert(gid);

            ResolvedWidget node = makeResolved(el);

            // Default the instance's tag to "group" so the painter
            // recurses into its body — without this, custom xuitag
            // instances (bento_tabbutton, sui_*, etc.) leave their
            // children unpainted because the painter only recognises
            // wasabi_* / group / container / layout as containers.
            // If the groupdef has `embed_xui="<basic-widget-tag>"` we
            // honour that and behave as that primitive (Bento.InfoLine
            // → text), which lets typed widgets render correctly.
            static const QSet<QString> kBasicXuiTags{
                QStringLiteral("text"),    QStringLiteral("button"),
                QStringLiteral("togglebutton"),
                QStringLiteral("layer"),   QStringLiteral("slider"),
                QStringLiteral("vis"),     QStringLiteral("edit"),
                QStringLiteral("list"),    QStringLiteral("grid"),
                QStringLiteral("songticker"), QStringLiteral("songtitle"),
                QStringLiteral("progressgrid"),
                QStringLiteral("nstatesbutton"),
                QStringLiteral("animatedlayer"),
                QStringLiteral("rect"),
            };
            const QString embedXui =
                def->attrs.value(QStringLiteral("embed_xui"));
            if (kBasicXuiTags.contains(embedXui.toLower())) {
                node.tag = embedXui.toLower();
            } else if (node.tag != QStringLiteral("group") &&
                       node.tag != QStringLiteral("container") &&
                       node.tag != QStringLiteral("layout")) {
                node.tag = QStringLiteral("group");
            }

            // `inherit_group="X"` on the groupdef: splice X's body in
            // before our own body and pick up X's attrs as defaults.
            // Recursively follows the chain so a chain of inherits is
            // collapsed in one pass.
            std::function<void(const Element *)> applyInherit;
            applyInherit = [&](const Element *d) {
                if (!d) return;
                const QString inh =
                    d->attrs.value(QStringLiteral("inherit_group"));
                if (inh.isEmpty()) return;
                const Element *p = m_groupdefs.lookup(inh);
                if (!p || m_inflightInstances.contains(inh)) return;
                m_inflightInstances.insert(inh);
                applyInherit(p);                  // walk further up first
                for (auto it = p->attrs.constBegin();
                     it != p->attrs.constEnd(); ++it) {
                    const QString &k = it.key();
                    if (k == QStringLiteral("id") ||
                        k == QStringLiteral("xuitag") ||
                        k == QStringLiteral("embed_xui") ||
                        k == QStringLiteral("inherit_group") ||
                        k == QStringLiteral("instanceid"))
                        continue;
                    if (!node.attrs.contains(k))
                        node.attrs.insert(k, it.value());
                }
                expandChildren(*p, node,
                               iid.isEmpty() ? instanceId : iid);
                m_inflightInstances.remove(inh);
            };
            applyInherit(def);

            // Inherit groupdef defaults the instance didn't override.
            // `<groupdef id="player.normal.display" relatw="1" w="-49">`
            // means the instance is sized -49 px shy of its container;
            // without this merge we'd treat instance w/h as 0 and the
            // child layers would size against the full canvas.
            for (auto it = def->attrs.constBegin();
                 it != def->attrs.constEnd(); ++it) {
                const QString &k = it.key();
                if (k == QStringLiteral("id") ||
                    k == QStringLiteral("xuitag") ||
                    k == QStringLiteral("embed_xui") ||
                    k == QStringLiteral("inherit_group") ||
                    k == QStringLiteral("instanceid"))
                    continue;
                if (!node.attrs.contains(k))
                    node.attrs.insert(k, it.value());
            }
            applySendparams(node, instanceId);
            expandChildren(*def, node,
                           iid.isEmpty() ? instanceId : iid);

            // `content="X"` on a frame instantiation says: inject the
            // groupdef X's children into this frame's content slot.
            // Wasabi's standardframe.maki normally does this at runtime;
            // we replicate it statically here so the player chrome
            // appears even without Maki running.  The `embed_xui`
            // attribute on the groupdef names a placeholder element
            // that gets replaced; without one, we just append.
            const QString content =
                el.attrs.value(QStringLiteral("content"));
            if (!content.isEmpty()) {
                if (const Element *contentDef = m_groupdefs.lookup(content)) {
                    if (!m_inflightInstances.contains(content)) {
                        m_inflightInstances.insert(content);
                        const QString embedTarget =
                            def->attrs.value(QStringLiteral("embed_xui"));
                        // Build the content's children into a temp node...
                        ResolvedWidget contentExp;
                        expandChildren(*contentDef, contentExp,
                                       iid.isEmpty() ? instanceId : iid);
                        // ...then either splice over the embed_xui
                        // placeholder, or append at the end.
                        if (!embedTarget.isEmpty()) {
                            replaceById(node, embedTarget, contentExp.children);
                        } else {
                            for (auto &c : contentExp.children)
                                node.children.append(std::move(c));
                        }
                        m_inflightInstances.remove(content);
                    }
                }
            }

            m_inflightInstances.remove(gid);
            parent.children.append(std::move(node));
            return;
        }

        // <Wasabi:Frame> — a built-in splitter with `left=`/`right=`
        // (orientation="v") or `top=`/`bottom=` (orientation="h")
        // attrs naming groupdef ids that fill each pane.  The skin
        // user can drag the divider; we statically place the two
        // panes at their default widths so the content widgets
        // referenced by Bento etc. actually render.
        if (el.tag == QStringLiteral("wasabi_frame")) {
            ResolvedWidget node = makeResolved(el);
            node.tag = QStringLiteral("group");
            applySendparams(node, instanceId);

            const QString orient =
                el.attrs.value(QStringLiteral("orientation")).toLower();
            const bool vertical =
                orient.isEmpty() || orient == QStringLiteral("v") ||
                orient == QStringLiteral("vertical");
            const QString first  = vertical
                ? el.attrs.value(QStringLiteral("left"))
                : el.attrs.value(QStringLiteral("top"));
            const QString second = vertical
                ? el.attrs.value(QStringLiteral("right"))
                : el.attrs.value(QStringLiteral("bottom"));
            const int defaultSize = vertical
                ? el.attrs.value(QStringLiteral("width")).toInt()
                : el.attrs.value(QStringLiteral("height")).toInt();

            auto addPane = [&](const QString &paneId, bool isFirst) {
                if (paneId.isEmpty()) return;
                if (!m_groupdefs.lookup(paneId)) return;
                Element pseudo;
                pseudo.tag = QStringLiteral("group");
                pseudo.attrs.insert(QStringLiteral("id"), paneId);
                if (defaultSize > 0) {
                    if (vertical) {
                        if (isFirst) {
                            pseudo.attrs.insert(QStringLiteral("x"), QStringLiteral("0"));
                            pseudo.attrs.insert(QStringLiteral("y"), QStringLiteral("0"));
                            pseudo.attrs.insert(QStringLiteral("w"),
                                QString::number(defaultSize));
                            pseudo.attrs.insert(QStringLiteral("h"), QStringLiteral("0"));
                            pseudo.attrs.insert(QStringLiteral("relath"), QStringLiteral("1"));
                        } else {
                            pseudo.attrs.insert(QStringLiteral("x"),
                                QString::number(defaultSize));
                            pseudo.attrs.insert(QStringLiteral("y"), QStringLiteral("0"));
                            pseudo.attrs.insert(QStringLiteral("w"),
                                QString::number(-defaultSize));
                            pseudo.attrs.insert(QStringLiteral("relatw"), QStringLiteral("1"));
                            pseudo.attrs.insert(QStringLiteral("h"), QStringLiteral("0"));
                            pseudo.attrs.insert(QStringLiteral("relath"), QStringLiteral("1"));
                        }
                    } else {
                        if (isFirst) {
                            pseudo.attrs.insert(QStringLiteral("x"), QStringLiteral("0"));
                            pseudo.attrs.insert(QStringLiteral("y"), QStringLiteral("0"));
                            pseudo.attrs.insert(QStringLiteral("w"), QStringLiteral("0"));
                            pseudo.attrs.insert(QStringLiteral("relatw"), QStringLiteral("1"));
                            pseudo.attrs.insert(QStringLiteral("h"),
                                QString::number(defaultSize));
                        } else {
                            pseudo.attrs.insert(QStringLiteral("x"), QStringLiteral("0"));
                            pseudo.attrs.insert(QStringLiteral("y"),
                                QString::number(defaultSize));
                            pseudo.attrs.insert(QStringLiteral("w"), QStringLiteral("0"));
                            pseudo.attrs.insert(QStringLiteral("relatw"), QStringLiteral("1"));
                            pseudo.attrs.insert(QStringLiteral("h"),
                                QString::number(-defaultSize));
                            pseudo.attrs.insert(QStringLiteral("relath"), QStringLiteral("1"));
                        }
                    }
                } else {
                    pseudo.attrs.insert(QStringLiteral("fitparent"),
                                        QStringLiteral("1"));
                }
                visit(pseudo, node, instanceId);
            };
            addPane(first,  /*isFirst=*/true);
            addPane(second, /*isFirst=*/false);

            parent.children.append(std::move(node));
            return;
        }

        // Regular element.
        ResolvedWidget node = makeResolved(el);
        applySendparams(node, instanceId);
        if (!el.children.isEmpty())
            expandChildren(el, node, instanceId);
        parent.children.append(std::move(node));
    }

    void applySendparams(ResolvedWidget &w, const QString &instanceId) {
        if (w.id.isEmpty()) return;
        if (m_hidden.contains(w.id)) {
            w.attrs.insert(QStringLiteral("visible"), QStringLiteral("0"));
        }
        // First instance-scoped sendparams (group="instanceid" target=…),
        // then layout-scoped (group="" target=…) — instance-scoped are
        // more specific and shouldn't be overridden by less-specific.
        // Actually upstream's order goes the other way: instance-scope
        // applies as the more recent override.  Match that.
        if (auto it = m_sendparams.constFind({QString(), w.id});
            it != m_sendparams.constEnd()) {
            for (auto kit = it->constBegin(); kit != it->constEnd(); ++kit)
                w.attrs.insert(kit.key(), kit.value());
        }
        if (!instanceId.isEmpty()) {
            if (auto it = m_sendparams.constFind({instanceId, w.id});
                it != m_sendparams.constEnd()) {
                for (auto kit = it->constBegin(); kit != it->constEnd(); ++kit)
                    w.attrs.insert(kit.key(), kit.value());
            }
        }
    }

    // Walk `node`'s tree, find the first descendant with `id == targetId`,
    // and replace its children with `replacement`.  Used to splice a
    // frame's `content` groupdef into the `embed_xui` placeholder slot.
    static bool replaceById(ResolvedWidget &node, const QString &targetId,
                            QList<ResolvedWidget> &replacement) {
        for (auto &c : node.children) {
            if (c.id == targetId) {
                c.children = std::move(replacement);
                return true;
            }
            if (replaceById(c, targetId, replacement)) return true;
        }
        return false;
    }

    const GroupdefIndex &m_groupdefs;
    const SendparamsMap &m_sendparams;
    const QSet<QString> &m_hidden;
    QSet<QString>        m_inflightInstances;
};

}  // namespace

bool expandLayout(const SkinXml::Document &doc,
                  const QString &containerId,
                  const QString &layoutId,
                  ResolvedWidget &out,
                  QString *errMsg) {
    const Element *container = findContainer(doc.root, containerId);
    if (!container) {
        if (errMsg) *errMsg = QStringLiteral("container '%1' not found")
                                   .arg(containerId);
        return false;
    }
    const Element *layout = findLayout(*container, layoutId);
    if (!layout) {
        if (errMsg) *errMsg = QStringLiteral(
            "container '%1' has no layout '%2'").arg(containerId, layoutId);
        return false;
    }

    GroupdefIndex groupdefs;
    collectGroupdefs(doc.root, groupdefs);
    // Sendparams come from two scopes that the static expansion
    // both cares about:
    //   1. Layout-scoped — `<sendparams target="window.titlebar.
    //      title" default="WACUP"/>` directly under a `<layout>`.
    //      We collect ONLY the chosen layout's, so the `default=
    //      "VIDEO"` override from another container doesn't leak
    //      into ours.
    //   2. Groupdef-scoped — `<sendparams group="wasabi.titlebar.
    //      streak.left" target="titlebar.center.active" w="-20"/>`
    //      inside a `<groupdef>`'s body.  These declare per-
    //      instance overrides of widgets inside that groupdef and
    //      MUST be picked up regardless of which layout we're
    //      expanding — without them the streak's center.active
    //      keeps its `w="-10"` default and overlaps right.active,
    //      which paints the right cap's bitmap design pixels on
    //      top of silver and produces visible 1-px dark seams.
    //   Walk groupdef bodies *first* so layout-scoped values
    //   override groupdef-scoped ones for the same target.
    SendparamsMap sendparams;
    {
        // Walk groupdef bodies but only pick up *instance-scoped*
        // sendparams (those with `group=<instanceid>`).  A
        // layout-scoped sendparams (`group=""`) sitting inside a
        // groupdef like `<groupdef id="wasabi.standardframe.modal">
        //   <sendparams target="wasabi.titlebar" x="4" w="-22"
        //   relatw="1"/></groupdef>` is meant to apply only when the
        // Modal frame is instantiated — collecting it globally
        // would re-position every wasabi.titlebar in the skin
        // (including the player's) by 6 px to the left.  The
        // instance-scoped ones, by contrast, target widgets inside
        // a specific instance (e.g. titlebar.center.active inside
        // wasabi.titlebar.streak.left) and are safe to apply
        // wherever that instance is expanded.
        std::function<void(const Element &)> walkInstanceScoped;
        std::function<void(const Element &)> visitChild =
            [&](const Element &el) {
            if (el.tag == QStringLiteral("sendparams")) {
                if (!el.attrs.value(QStringLiteral("group")).isEmpty())
                    collectSendparams(el, sendparams);
                return;
            }
            for (const auto &c : el.children) visitChild(c);
        };
        walkInstanceScoped = [&](const Element &el) {
            if (el.tag == QStringLiteral("groupdef")) {
                for (const auto &c : el.children) visitChild(c);
            }
            for (const auto &c : el.children) walkInstanceScoped(c);
        };
        walkInstanceScoped(doc.root);
    }
    collectSendparams(*layout, sendparams);
    QSet<QString> hidden;
    collectHideObjects(doc.root, hidden);
    // Wasabi's standardframe.maki / videoavs.maki / pledit.maki etc.
    // call setVisible(0) on these container groups during onLoad.
    // Until our Maki bindings actually run those scripts, treat the
    // groups as default-hidden so static rendering produces a sane
    // approximation of the in-app render.
    static const QStringList kScriptHiddenByDefault {
        // The drawer + shadow are *script-toggled* in real Wasabi
        // (CONFIG button click runs setVisible(1)).  Show them by
        // default so the EQ / options / colour-themes pages live in
        // the chrome at startup; the toggle binding can flip them
        // off later when the runtime drives it.
        QStringLiteral("AVSGroup"),
        QStringLiteral("player.normal.video"),
        QStringLiteral("player.shade.drawer"),
        // configtabs.m toggles the on/off variants per tab.  We
        // emulate the EQ-selected state below by hiding the EQ's
        // .off variant and the Options/ColorThemes' .on variants;
        // the tab cluster itself renders.  Non-EQ content pages
        // stay hidden so only the EQ panel is in view at startup.
        QStringLiteral("config.tab.eq.off"),
        QStringLiteral("config.tab.options.on"),
        QStringLiteral("config.tab.colorthemes.on"),
        QStringLiteral("player.normal.drawer.options"),
        QStringLiteral("player.normal.drawer.colorthemes"),
    };
    for (const auto &id : kScriptHiddenByDefault) hidden.insert(id);

    out = makeResolved(*layout);
    Expander ex(groupdefs, sendparams, hidden);
    ex.expandChildren(*layout, out, /*instanceId*/ {});

    // Static well-known-script equivalents (titlebar resizeObjects
    // etc.) used to run here so the resolved tree was paint-ready
    // immediately.  M14b moves that responsibility to whoever owns
    // the post-resolve mutation step: callers that drive Maki via
    // SkinRuntime get widget mutations from the dispatched scripts;
    // callers that want the legacy static path can call
    // `runKnownScripts(out, layoutW)` themselves before rendering.

    return true;
}

// ──────────────────────────────────────────────────────────────────
// Static equivalents of well-known Maki scripts.  Each does the
// minimum geometry/visibility manipulation a script's onScriptLoaded
// + onResize handlers would otherwise do at runtime.  Removed once
// real Maki bindings ship in M13.

namespace knownscripts {

ResolvedWidget *findById(ResolvedWidget &w, const QString &id) {
    if (w.id == id) return &w;
    for (auto &c : w.children)
        if (auto *r = findById(c, id)) return r;
    return nullptr;
}

ResolvedWidget *findByTag(ResolvedWidget &w, const QString &tag) {
    if (w.tag == tag) return &w;
    for (auto &c : w.children)
        if (auto *r = findByTag(c, tag)) return r;
    return nullptr;
}

// Wasabi's titlebar.m runs `resizeObjects()` on script-load + every
// onResize.  Without scripts, the static streak.left x=0 w=95, text
// x=100 w=50, streak.right x=155 w=-155 positions are wrong for the
// rendered text width.  Mirror the script's logic statically.
//
// Walks every <Wasabi:TitleBar> instance (xuitag-aliased to
// `wasabi.titlebar` groupdef), finds its three streak children +
// title text, and re-positions them based on the enclosing frame's
// padtitleleft/padtitleright XUI params + the layout's actual width.
void applyTitlebarResize(ResolvedWidget &titlebar,
                         int layoutWidth,
                         int titlebarLayoutX,
                         int padLeft, int padRight) {
    auto *streakL = findById(titlebar, QStringLiteral("wasabi.titlebar.streak"));
    // Two instances exist (left + right) — distinguish by instanceId.
    ResolvedWidget *streakRight = nullptr;
    {
        std::function<void(ResolvedWidget &)> walk = [&](ResolvedWidget &w) {
            if (w.id == QStringLiteral("wasabi.titlebar.streak")) {
                if (w.instanceId.endsWith(QStringLiteral(".left"))) {
                    streakL = &w;
                } else if (w.instanceId.endsWith(QStringLiteral(".right"))) {
                    streakRight = &w;
                }
            }
            for (auto &c : w.children) walk(c);
        };
        walk(titlebar);
    }
    auto *title = findById(titlebar, QStringLiteral("window.titlebar.title"));
    auto *titleOverlay = findById(titlebar,
                                  QStringLiteral("window.titlebar.title.overlay"));

    // Real text width from QFontMetrics, mirroring what
    // wq_widget_textWidth (and Wasabi's Text::getPreferences
    // SUGGESTED_W) compute for the Maki getAutoWidth binding:
    // glyph advance + 4 per-segment (Wasabi convention) + 7 px to
    // bridge the Win32-GDI / Qt-QFontMetrics gap for Arial Bold at
    // the converted pixel size — the same constants libwasabiq
    // applies (see commits 5fac3c25 + 7cf705eb).  Without these,
    // the streak gap math runs against a smaller text width than
    // is actually painted and the right streak overlaps the title.
    int textWidth = 0;
    if (title) {
        QString s = title->attrs.value(QStringLiteral("text"));
        if (s.isEmpty()) s = title->attrs.value(QStringLiteral("default"));
        if (title->attrs.value(QStringLiteral("forceuppercase")) ==
            QStringLiteral("1"))
            s = s.toUpper();
        if (!s.isEmpty()) {
            QFont f;
            const QString family = title->attrs.value(
                QStringLiteral("font"));
            if (!family.isEmpty()) f.setFamily(family);
            bool ok = false;
            const int fontsize = title->attrs.value(
                QStringLiteral("fontsize")).toInt(&ok);
            if (ok && fontsize > 0)
                f.setPixelSize(qMax(1, (fontsize * 5 + 3) / 7));
            if (title->attrs.value(QStringLiteral("bold")) ==
                QStringLiteral("1"))
                f.setBold(true);
            QFontMetrics fm(f);
            textWidth = fm.horizontalAdvance(s) + 9;
        }
    }
    if (textWidth <= 0) textWidth = 50;

    // Mirror titlebar.m::resizeObjects() exactly:
    //
    //   lx = (layout_width - text_width) / 2;     // layout coords
    //   lx -= sg.getLeft();                       // → titlebar-local
    //   center.setXmlParam("x", lx + cen);        // cen = 2
    //   left.setXmlParam ("x", padleft);
    //   left.setXmlParam ("w", lx - padleft);
    //   right.setXmlParam("x", lx + text_width + 1);
    //   right.setXmlParam("w", -(lx + text_width + padright + 2));
    //   right.setXmlParam("relatw", "1");
    //
    // The earlier version used the titlebar's own width as the
    // centring base (`innerW`).  That centred the title within the
    // titlebar group instead of within the layout, which on
    // off-centre frames (Wasabi:MainFrame:NoStatus has the titlebar
    // at x=10, w=relative-29) shifted the text plus both streaks
    // off-axis.
    const int cen = 2;
    const int lx = (layoutWidth - textWidth) / 2 - titlebarLayoutX;

    if (title) {
        title->attrs.insert(QStringLiteral("x"), QString::number(lx + cen));
        title->attrs.insert(QStringLiteral("w"), QString::number(textWidth));
        title->attrs.remove(QStringLiteral("relatx"));
        title->attrs.remove(QStringLiteral("relatw"));
    }
    if (titleOverlay) {
        titleOverlay->attrs.insert(QStringLiteral("x"),
                                    QString::number(lx + cen));
        titleOverlay->attrs.insert(QStringLiteral("w"),
                                    QString::number(textWidth));
    }
    if (streakL) {
        streakL->attrs.insert(QStringLiteral("x"), QString::number(padLeft));
        streakL->attrs.insert(QStringLiteral("w"),
                               QString::number(lx - padLeft));
        streakL->attrs.remove(QStringLiteral("relatx"));
        streakL->attrs.remove(QStringLiteral("relatw"));
    }
    if (streakRight) {
        const int rightX = lx + textWidth + 1;
        streakRight->attrs.insert(QStringLiteral("x"),
                                   QString::number(rightX));
        // titlebar.m emits w = -(lx + text_w + 1 + padright + 1) =
        // -(rightX + padright + 1) with relatw=1 — the negative
        // value plus relatw makes the streak end `padright + 1`
        // pixels short of the titlebar's right edge.
        const int rightWNeg = -(rightX + padRight + 1);
        streakRight->attrs.insert(QStringLiteral("w"),
                                   QString::number(rightWNeg));
        streakRight->attrs.insert(QStringLiteral("relatw"),
                                   QStringLiteral("1"));
        streakRight->attrs.remove(QStringLiteral("relatx"));
    }
}

// Walk the resolved tree, find <Wasabi:TitleBar> instances, run the
// titlebar resize equivalent against each.  Tracks the accumulated
// layout-x of each container we descend into so the titlebar's
// own offset (e.g. x=10 inside Wasabi:MainFrame:NoStatus) can be
// fed to applyTitlebarResize as `sg.getLeft()` would in the script.
void applyTo(ResolvedWidget &root, int layoutWidth) {
    std::function<void(ResolvedWidget &, int, int, int)> walk =
        [&](ResolvedWidget &w, int xOffset, int padLeft, int padRight) {
        // Capture XUI params from frame instantiations as scoped
        // overrides for nested titlebars.
        int newPadLeft  = padLeft;
        int newPadRight = padRight;
        if (w.attrs.contains(QStringLiteral("padtitleleft"))) {
            newPadLeft += w.attrs.value(
                QStringLiteral("padtitleleft")).toInt();
        }
        if (w.attrs.contains(QStringLiteral("padtitleright"))) {
            newPadRight += w.attrs.value(
                QStringLiteral("padtitleright")).toInt();
        }

        // The titlebar groupdef has id "wasabi.titlebar" and
        // xuitag "Wasabi:TitleBar" (normalised: wasabi_titlebar).
        if (w.tag == QStringLiteral("wasabi_titlebar") ||
            w.id  == QStringLiteral("wasabi.titlebar")) {
            const int titlebarX = xOffset +
                w.attrs.value(QStringLiteral("x")).toInt();
            applyTitlebarResize(w, layoutWidth, titlebarX,
                                newPadLeft, newPadRight);
        }

        // Descend into children.  Containers ([group/container/
        // groupdef]) push their own x onto the running offset so a
        // nested titlebar sees its true layout-x.  Other widgets
        // pass the offset through unchanged — they don't translate.
        const bool descends =
            w.tag == QStringLiteral("group") ||
            w.tag == QStringLiteral("container") ||
            w.tag == QStringLiteral("groupdef") ||
            w.tag == QStringLiteral("layout") ||
            w.tag.startsWith(QStringLiteral("wasabi_"));
        const int childOffset = descends
            ? xOffset + w.attrs.value(QStringLiteral("x")).toInt()
            : xOffset;
        for (auto &c : w.children)
            walk(c, childOffset, newPadLeft, newPadRight);
    };
    walk(root, 0, 0, 0);
}

}  // namespace knownscripts

void runKnownScripts(ResolvedWidget &root, int layoutWidth) {
    knownscripts::applyTo(root, layoutWidth);

    // Position the config drawer below the player chrome.  The
    // drawer's XML declares `y="-263" relaty="1"` which lands it at
    // y=17 inside a 280-tall layout — directly under the chrome
    // and so visually hidden behind player.main.  Real Wasabi's
    // pbswitch.maki sets a runtime y when the user toggles the
    // CONFIG button; until SkinRuntime drives that, place the
    // drawer right below player.main (h=126 + y=17 ≈ 143) so its
    // EQ / Options / Color-Themes pages are visible.  Same shape
    // hint (ahead of the chrome bottom) for drawer.shadow which
    // lives just above the drawer's bottom edge.
    std::function<void(ResolvedWidget &)> walk =
        [&](ResolvedWidget &w) {
        if (w.id == QStringLiteral("player.normal.drawer")) {
            // configtabs.m::OpenDrawer sets drawer y=-147 with
            // relaty=1 → for layout h=280 that is y=133.  The 10
            // px difference vs the player.main bottom (y=143)
            // tucks the drawer's top into the player chrome's
            // toggle notch — without it, the area below the
            // CONFIG text shows the chrome's transparent notch
            // instead of the drawer chrome.
            w.attrs.insert(QStringLiteral("y"),
                           QStringLiteral("133"));
            w.attrs.remove(QStringLiteral("relaty"));
        } else if (w.id == QStringLiteral("player.normal.drawer.shadow")) {
            w.attrs.insert(QStringLiteral("y"),
                           QStringLiteral("121"));
            w.attrs.remove(QStringLiteral("relaty"));
        } else if (w.id == QStringLiteral("player.normal.drawer.content")) {
            // configtabs.m's main.onResize handler centres
            // DrawerContent inside main:
            //   newXpos = w/2 - 163;
            //   DrawerContent.setXmlParam("x", newXpos);
            // dispatchInitialResize is wired in SkinRuntime + the Maki
            // bridge but the handler currently reads the wrong arg
            // slot (gets w=652 instead of 354); apply statically until
            // the Maki event-arg ordering is sorted out.
            const int newX = layoutWidth / 2 - 163;
            w.attrs.insert(QStringLiteral("x"),
                           QString::number(newX));
        }
        for (auto &c : w.children) walk(c);
    };
    walk(root);

    // Resolve `autowidthsource=` into a real `w` attribute on any
    // group/widget that has it but no explicit `w`.  Wasabi normally
    // does this at render time as part of the auto-width pass; our
    // renderer doesn't yet, but the layout values are needed both for
    // the renderer's 3-slice chrome and for scripts that read
    // .getWidth() then position siblings (configtabs.m).  Keeping the
    // resolution here means the script side stays generic — no
    // skin-specific x/w cluster math needed.
    std::function<void(ResolvedWidget &)> resolveAutoWidth =
        [&](ResolvedWidget &w) {
        const QString aws =
            w.attrs.value(QStringLiteral("autowidthsource"));
        if (!aws.isEmpty()) {
            const QString curW = w.attrs.value(QStringLiteral("w"));
            if (curW.isEmpty() || curW.toInt() <= 0) {
                // Find the referenced text widget anywhere in the tree.
                std::function<ResolvedWidget *(ResolvedWidget &)> findById =
                    [&](ResolvedWidget &n) -> ResolvedWidget * {
                    if (n.id == aws) return &n;
                    for (auto &c : n.children)
                        if (auto *r = findById(c)) return r;
                    return nullptr;
                };
                ResolvedWidget *src = findById(root);
                if (src) {
                    const QString s =
                        src->attrs.value(QStringLiteral("default"));
                    if (!s.isEmpty()) {
                        // Use the bitmap-font width heuristic
                        // (charwidth=6 + 24 for padding).  Falls
                        // back to roughly Arial Bold metrics for
                        // non-bitmap fonts.
                        const QString family =
                            src->attrs.value(QStringLiteral("font"));
                        int width = 0;
                        if (family.startsWith(QStringLiteral("player.")) ||
                            family.startsWith(QStringLiteral("drawer.")) ||
                            family.contains(QStringLiteral("smallfont")) ||
                            family.contains(QStringLiteral("bitmapfont"))) {
                            width = s.size() * 6 + 24;
                        } else {
                            QFont f;
                            if (!family.isEmpty()) f.setFamily(family);
                            bool ok = false;
                            const int fs = src->attrs
                                .value(QStringLiteral("fontsize"))
                                .toInt(&ok);
                            if (ok && fs > 0)
                                f.setPixelSize(qMax(1, (fs * 5 + 3) / 7));
                            if (src->attrs.value(QStringLiteral("bold")) ==
                                QStringLiteral("1"))
                                f.setBold(true);
                            QFontMetrics fm(f);
                            width = fm.horizontalAdvance(s) + 24;
                        }
                        if (width > 0)
                            w.attrs.insert(QStringLiteral("w"),
                                           QString::number(width));
                    }
                }
            }
        }
        for (auto &c : w.children) resolveAutoWidth(c);
    };
    resolveAutoWidth(root);
}

QStringList containerIds(const SkinXml::Document &doc) {
    QStringList ids;
    std::function<void(const Element &)> walk = [&](const Element &el) {
        if (el.tag == QStringLiteral("container")) {
            const QString id = el.attrs.value(QStringLiteral("id"));
            if (!id.isEmpty()) ids.append(id);
        }
        for (const auto &c : el.children) walk(c);
    };
    walk(doc.root);
    return ids;
}

QStringList layoutIds(const SkinXml::Document &doc, const QString &containerId) {
    QStringList ids;
    const Element *c = findContainer(doc.root, containerId);
    if (!c) return ids;
    for (const auto &child : c->children) {
        if (child.tag == QStringLiteral("layout")) {
            const QString id = child.attrs.value(QStringLiteral("id"));
            if (!id.isEmpty()) ids.append(id);
        }
    }
    return ids;
}

// Hit-test recurser.  Walks children in reverse paint order (last
// child = topmost) so the deepest visible match wins.  Mirrors
// TreePainter's resolveRect: applies relatx / relaty / relatw /
// relath against the parent's canvas size, and translates groups
// by their resolved (x, y) the same way the painter does.  Falls
// back to an image-size resolver when a widget has no explicit
// `w`/`h` (typical for buttons whose size comes from their named
// bitmap).
namespace {
bool boolAttr(const QHash<QString, QString> &a, const QString &k) {
    const QString v = a.value(k);
    return v == QStringLiteral("1") || v.compare(QStringLiteral("true"),
                                                 Qt::CaseInsensitive) == 0;
}

QRect resolveRect(const QHash<QString, QString> &a, QSize parent) {
    int x = a.value(QStringLiteral("x")).toInt();
    int y = a.value(QStringLiteral("y")).toInt();
    int w = a.value(QStringLiteral("w")).toInt();
    int h = a.value(QStringLiteral("h")).toInt();
    bool rx = boolAttr(a, QStringLiteral("relatx"));
    bool ry = boolAttr(a, QStringLiteral("relaty"));
    bool rw = boolAttr(a, QStringLiteral("relatw"));
    bool rh = boolAttr(a, QStringLiteral("relath"));
    // `fitparent="1"` is a Wasabi shortcut meaning "fill the parent
    // in both dimensions" — i.e. x=0 y=0 w=0 h=0 relatw=1 relath=1
    // — that explicit per-axis attrs may still override.  Bento's
    // tab grids and SUI panels rely on this without spelling out the
    // relat-w/h flags.
    if (boolAttr(a, QStringLiteral("fitparent"))) {
        if (!a.contains(QStringLiteral("w"))) rw = true;
        if (!a.contains(QStringLiteral("h"))) rh = true;
    }
    if (rx) x = parent.width()  + x;
    if (ry) y = parent.height() + y;
    if (rw) w = parent.width()  + w;
    if (rh) h = parent.height() + h;
    return QRect(x, y, w, h);
}

bool isContainer(const QString &tag) {
    return tag == QStringLiteral("group")     ||
           tag == QStringLiteral("container") ||
           tag == QStringLiteral("layout")    ||
           tag == QStringLiteral("groupdef");
}

const ResolvedWidget *hitTestRec(const ResolvedWidget &w,
                                 QPoint p, QPoint origin, QSize canvas,
                                 bool actionOnly,
                                 ImageSizeResolver resolver,
                                 void *userdata,
                                 QRect *outBbox) {
    // Hidden widgets (visible="0") don't participate in hit-test
    // either, just like painting.  Without this, off-screen helper
    // groups like AVSGroup intercept clicks on the visible chrome
    // (their bg layer spans most of the layout).
    if (w.attrs.value(QStringLiteral("visible")) ==
        QStringLiteral("0"))
        return nullptr;
    const QRect r = resolveRect(w.attrs, canvas);

    // Layout: doesn't translate, but propagates its own size to
    // children.  Other containers (group/container/groupdef)
    // translate by (r.x, r.y).
    QPoint childOrigin = origin;
    if (w.tag != QStringLiteral("layout")) {
        childOrigin = QPoint(origin.x() + r.x(), origin.y() + r.y());
    }
    QSize childCanvas = canvas;
    if (r.width()  > 0) childCanvas.setWidth (r.width());
    if (r.height() > 0) childCanvas.setHeight(r.height());

    // Recurse into children first — topmost match wins.
    for (auto it = w.children.crbegin(); it != w.children.crend(); ++it) {
        if (auto *hit = hitTestRec(*it, p, childOrigin, childCanvas,
                                    actionOnly, resolver, userdata,
                                    outBbox))
            return hit;
    }

    if (actionOnly &&
        !w.attrs.contains(QStringLiteral("action"))) {
        return nullptr;
    }

    // Self bbox: prefer explicit/resolved w/h, fall back to
    // bitmap-image dimensions for widgets without sizes.
    int width  = r.width();
    int height = r.height();
    if ((width <= 0 || height <= 0) && resolver) {
        const QString img = w.attrs.value(QStringLiteral("image"));
        if (!img.isEmpty()) {
            const QSize imgSize = resolver(img, userdata);
            if (width  <= 0) width  = imgSize.width();
            if (height <= 0) height = imgSize.height();
        }
    }
    if (width <= 0 || height <= 0) return nullptr;

    // Container widgets are usually transparent — their hits would
    // shadow their children.  Since we recurse children first, we
    // only reach a container when no child caught the click; hits
    // on bare group regions do nothing useful, so skip them.
    if (isContainer(w.tag)) return nullptr;

    const QRect bbox(childOrigin.x(), childOrigin.y(), width, height);
    if (!bbox.contains(p)) return nullptr;
    if (outBbox) *outBbox = bbox;
    return &w;
}
}  // namespace

const ResolvedWidget *hitTest(const ResolvedWidget &root,
                              QPoint pointInLayout,
                              bool actionOnly,
                              ImageSizeResolver imageSize,
                              void *imageSizeUserdata,
                              QRect *outBbox) {
    QSize rootCanvas(
        root.attrs.value(QStringLiteral("w")).toInt(),
        root.attrs.value(QStringLiteral("h")).toInt());
    if (rootCanvas.width()  <= 0 || rootCanvas.height() <= 0) {
        rootCanvas = QSize(354, 280);  // safe Modern-skin default
    }
    return hitTestRec(root, pointInLayout, QPoint(0, 0), rootCanvas,
                      actionOnly, imageSize, imageSizeUserdata, outBbox);
}

// ── Window-region builder ─────────────────────────────────────────
namespace {
// Walk the tree, painting only sysregion-tagged layers onto `out`.
// Wasabi convention: any layer with `sysregion` != 0 contributes
// its opaque pixels to the window region.  The actual numeric value
// (1, -2, …) controls subtleties like alpha-edge anti-aliasing that
// we don't yet implement — for now we treat all non-zero values
// the same way (opaque pixels = part of region).
void paintRegionLayers(QPainter &p, const ResolvedWidget &w,
                       BitmapRegistry &reg, QSize canvas,
                       bool &outFoundAny) {
    if (w.attrs.value(QStringLiteral("visible")) ==
        QStringLiteral("0")) {
        return;
    }

    const QRect r = resolveRect(w.attrs, canvas);
    QSize childCanvas = canvas;
    if (r.width()  > 0) childCanvas.setWidth (r.width());
    if (r.height() > 0) childCanvas.setHeight(r.height());

    const bool isContainer =
        w.tag == QStringLiteral("group")     ||
        w.tag == QStringLiteral("container") ||
        w.tag == QStringLiteral("groupdef")  ||
        w.tag == QStringLiteral("layout")    ||
        w.tag.startsWith(QStringLiteral("wasabi_"));

    // Group-level `sysregion="1"` makes the group's *rectangle*
    // part of the window region.  Modern's drawer declares
    // `<group id="player.normal.drawer" sysregion="1">` so the
    // chrome can host the EQ/options pages below the player.
    //
    // IMPORTANT: fill BEFORE applying our own translate, because
    // `r` is in the *parent's* coord space (resolveRect ran with
    // parent canvas).  Filling after translate would double-shift
    // (paint at parent_pos + r) and miss the actual group rect —
    // the drawer's rect ends up off-screen and the cutout layers
    // then act on a buffer that was never marked as in-region.
    // Bento (and some other Wasabi 2 skins) uses `regionop=` as a
    // synonym for `sysregion=` on the same widgets — same semantics,
    // different attribute name.  Treat either as the region directive.
    auto regionAttr = [&]() {
        const QString sr = w.attrs.value(QStringLiteral("sysregion"));
        if (!sr.isEmpty()) return sr;
        return w.attrs.value(QStringLiteral("regionop"));
    };
    if (isContainer && r.width() > 0 && r.height() > 0) {
        const QString sr = regionAttr();
        if (sr == QStringLiteral("1")) {
            p.fillRect(r, Qt::black);
            outFoundAny = true;
        }
    }

    const bool translate = isContainer &&
                           w.tag != QStringLiteral("layout") &&
                           (r.x() != 0 || r.y() != 0);
    if (translate) p.save(), p.translate(r.x(), r.y());

    // Layer with sysregion -> contribute its bitmap pixels.
    //
    // Wasabi convention:
    //   sysregion="1"  → opaque pixels of this layer are part of
    //                    the window region.  Painted SourceOver,
    //                    accumulating into the union.
    //   sysregion="-2" → opaque pixels of this layer are CUTOUTS
    //                    that must be removed from the region.
    //                    These are the staircase corner masks
    //                    (mainregions.png 6x6 patterns) — opaque
    //                    pixels mark the rounded-out corner area.
    //                    Painted with DestinationOut so each
    //                    opaque mask pixel zeroes the destination
    //                    alpha at that position — that's how the
    //                    chrome rectangles get their rounded
    //                    corners.

    if (w.tag == QStringLiteral("layer")) {
        const QString sr = regionAttr();
        if (!sr.isEmpty() && sr != QStringLiteral("0")) {
            const QString id  = w.attrs.value(QStringLiteral("id"));
            const QString img = w.attrs.value(QStringLiteral("image"));
            if (::getenv("WASABIQT_DEBUG_SYSREGION")) {
                fprintf(stderr,
                    "[sysregion] sr=%s id=%s image=%s rect=%dx%d+%d+%d\n",
                    sr.toLocal8Bit().constData(),
                    id.toLocal8Bit().constData(),
                    img.toLocal8Bit().constData(),
                    r.width(), r.height(), r.x(), r.y());
            }
            // Negative sysregion values are cutouts: opaque bitmap
            // pixels remove the corresponding pixels from the window
            // region.  Modern's drawer ships its rounded bottom-
            // corner masks as `sysregion="-1"`; the player chrome's
            // rounded corners use `sysregion="-2"`.  Treat any
            // negative value as a cutout (DestinationOut clears the
            // region buffer's alpha at those pixels).
            const bool cutoutMode = sr.startsWith(QChar('-'));
            if (cutoutMode)
                p.setCompositionMode(
                    QPainter::CompositionMode_DestinationOut);
            LayerPainter::paintLayer(&p, reg, w.attrs, canvas);
            if (cutoutMode)
                p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            outFoundAny = true;
        }
    }

    for (const auto &c : w.children)
        paintRegionLayers(p, c, reg, childCanvas, outFoundAny);

    if (translate) p.restore();
}

QRegion regionFromAlpha(const QImage &img) {
    // Build a QRegion directly as a union of horizontal row spans
    // wherever the source pixel has non-zero alpha.  Skipping the
    // QBitmap intermediate avoids a Qt path that ends up empty on
    // some configurations.
    QRegion region;
    const int W = img.width(), H = img.height();
    int countNonzero = 0;
    for (int y = 0; y < H; ++y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(
            img.constScanLine(y));
        int spanStart = -1;
        for (int x = 0; x < W; ++x) {
            const bool inside = qAlpha(src[x]) > 0;
            if (inside) {
                if (spanStart < 0) spanStart = x;
                ++countNonzero;
            } else if (spanStart >= 0) {
                region += QRect(spanStart, y, x - spanStart, 1);
                spanStart = -1;
            }
        }
        if (spanStart >= 0)
            region += QRect(spanStart, y, W - spanStart, 1);
    }
    if (::getenv("WASABIQT_DEBUG_SYSREGION")) {
        fprintf(stderr, "[sysregion] non-zero alpha pixels: %d / %d, "
                        "region rectCount=%d boundingRect=%dx%d+%d+%d\n",
                countNonzero, W * H,
                region.rectCount(),
                region.boundingRect().width(),
                region.boundingRect().height(),
                region.boundingRect().x(),
                region.boundingRect().y());
    }
    return region;
}
}  // namespace

QRegion computeWindowRegion(const ResolvedWidget &root,
                            BitmapRegistry &registry,
                            QSize canvas) {
    if (canvas.width() <= 0 || canvas.height() <= 0)
        return QRegion();

    QImage buf(canvas, QImage::Format_ARGB32_Premultiplied);
    buf.fill(Qt::transparent);
    bool foundAny = false;
    {
        QPainter p(&buf);
        // Disable anti-aliasing + smooth scaling so sysregion=-2
        // mask bitmaps draw 1:1 without alpha bleeding.
        p.setRenderHint(QPainter::Antialiasing,            false);
        p.setRenderHint(QPainter::SmoothPixmapTransform,   false);
        paintRegionLayers(p, root, registry, canvas, foundAny);
    }
    if (::getenv("WASABIQT_DEBUG_SYSREGION_DUMP")) {
        buf.save("/tmp/qtwasabi-region.png");
        fprintf(stderr, "[sysregion] region buffer saved to "
                        "/tmp/qtwasabi-region.png (foundAny=%d)\n",
                foundAny ? 1 : 0);
    }
    if (!foundAny) return QRegion();
    return regionFromAlpha(buf);
}

}  // namespace WasabiQt::Layout
