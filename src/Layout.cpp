// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/HitCtx.h>
#include <WasabiQt/Layout.h>
#include <WasabiQt/SkinXml.h>
#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/LayerPainter.h>
#include <WasabiQt/Widget.h>

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

// makeResolved — instantiate a Widget subclass appropriate to `src.tag`
// (Widget::create factory) and copy the element's attrs onto it.  The
// result is owned by the caller; typical pattern:
//
//   auto nodePtr = makeResolved(el);
//   /* ... populate nodePtr->children ... */
//   parent.children.push_back(std::move(nodePtr));
//
// Returning a unique_ptr (rather than ResolvedWidget by value) is
// required for the polymorphic tree — Widget subclasses can't be
// value-stored or copied because of the unique_ptr-of-children field
// (move-only) and the abstract-base-via-children pattern.
std::unique_ptr<Widget> makeResolved(const Element &src,
                                     const QString &tagOverride = {}) {
    const QString &tag = tagOverride.isEmpty() ? src.tag : tagOverride;
    auto r = Widget::create(tag);
    r->tag        = tag;
    r->id         = src.attrs.value(QStringLiteral("id"));
    r->instanceId = src.attrs.value(QStringLiteral("instanceid"));
    r->attrs      = src.attrs;
    r->sourceFile = src.sourceFile;
    r->sourceLine = src.sourceLine;
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

            // Default the instance's tag to "group" so the painter
            // recurses into its body — without this, custom xuitag
            // instances (bento_tabbutton, sui_*, etc.) leave their
            // children unpainted because the painter only recognises
            // wasabi_* / group / container / layout as containers.
            // If the groupdef has `embed_xui="<basic-widget-tag>"` we
            // honour that and behave as that primitive (Bento.InfoLine
            // → text), which lets typed widgets render correctly.
            //
            // Resolve the FINAL tag before instantiating so the factory
            // picks the correct Widget subclass for paint dispatch —
            // rewriting `node.tag` post-construction would leave the
            // wrong subclass painting the wrong widget.
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
            QString finalTag = el.tag;
            const QString embedXui =
                def->attrs.value(QStringLiteral("embed_xui"));
            if (kBasicXuiTags.contains(embedXui.toLower())) {
                finalTag = embedXui.toLower();
            } else if (finalTag != QStringLiteral("group") &&
                       finalTag != QStringLiteral("container") &&
                       finalTag != QStringLiteral("layout")) {
                finalTag = QStringLiteral("group");
            }

            auto nodePtr = makeResolved(el, finalTag);
            Widget &node = *nodePtr;

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
                        Widget contentExp;
                        expandChildren(*contentDef, contentExp,
                                       iid.isEmpty() ? instanceId : iid);
                        // ...then either splice over the embed_xui
                        // placeholder, or append at the end.
                        if (!embedTarget.isEmpty()) {
                            replaceById(node, embedTarget, contentExp.children);
                        } else {
                            for (auto &c : contentExp.children)
                                node.children.push_back(std::move(c));
                        }
                        m_inflightInstances.remove(content);
                    }
                }
            }

            m_inflightInstances.remove(gid);
            parent.children.push_back(std::move(nodePtr));
            return;
        }

        // <Wasabi:Frame> — a built-in splitter with `left=`/`right=`
        // (orientation="v") or `top=`/`bottom=` (orientation="h")
        // attrs naming groupdef ids that fill each pane.  The skin
        // user can drag the divider; we statically place the two
        // panes at their default widths so the content widgets
        // referenced by Bento etc. actually render.
        if (el.tag == QStringLiteral("wasabi_frame")) {
            auto nodePtr = makeResolved(el, QStringLiteral("group"));
            Widget &node = *nodePtr;
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

            parent.children.push_back(std::move(nodePtr));
            return;
        }

        // Regular element.
        auto nodePtr = makeResolved(el);
        Widget &node = *nodePtr;
        applySendparams(node, instanceId);
        if (!el.children.isEmpty())
            expandChildren(el, node, instanceId);
        // <componentbucket wndtype="X">: materialise every groupdef in
        // the document whose `windowtype="X"` as a positioned child
        // inside the bucket.  Real Wasabi does this at runtime via
        // ComponentBucket::addChild + scrolling; we statically inject
        // them here so TreePainter walks them like any other group.
        // The bucket's spacing/leftmargin/rightmargin control the per-
        // entry offsets; `vertical=1` stacks them down, otherwise
        // across.  Without this materialisation, the entire config-
        // drawer "Options" page is a blank box (the bucket is empty).
        if (el.tag == QStringLiteral("componentbucket")) {
            const QString want = node.attrs.value(QStringLiteral("wndtype"));
            if (!want.isEmpty()) {
                const bool vertical =
                    node.attrs.value(QStringLiteral("vertical")) ==
                    QStringLiteral("1");
                const int spacing =
                    node.attrs.value(QStringLiteral("spacing")).toInt();
                const int leftMargin =
                    node.attrs.value(QStringLiteral("leftmargin")).toInt();
                const int topMargin =
                    node.attrs.value(QStringLiteral("topmargin")).toInt();
                int offset = vertical ? topMargin : leftMargin;
                // Sort matching groupdef ids so the bucket entry order
                // is deterministic (declared bucket.entry.1, .2, .3, …
                // rather than QHash iteration order).
                QStringList ids;
                for (auto it = m_groupdefs.byId.constBegin();
                     it != m_groupdefs.byId.constEnd(); ++it) {
                    if (it.value()->attrs.value(
                            QStringLiteral("windowtype")) == want)
                        ids.append(it.key());
                }
                std::sort(ids.begin(), ids.end());
                int step = 0;
                for (const QString &gid : ids) {
                    const Element *def = m_groupdefs.byId.value(gid);
                    if (!def) continue;
                    // Synthesize a `<group id="<gd>"/>` instance so the
                    // existing groupdef-resolution path handles
                    // expansion (attrs merge, sendparams, recursion).
                    Element instance;
                    instance.tag = QStringLiteral("group");
                    instance.attrs.insert(QStringLiteral("id"), gid);
                    if (vertical) {
                        instance.attrs.insert(QStringLiteral("x"),
                            QString::number(leftMargin));
                        instance.attrs.insert(QStringLiteral("y"),
                            QString::number(offset));
                    } else {
                        instance.attrs.insert(QStringLiteral("x"),
                            QString::number(offset));
                        instance.attrs.insert(QStringLiteral("y"),
                            QString::number(topMargin));
                    }
                    visit(instance, node, instanceId);
                    step =
                        def->attrs.value(vertical ? QStringLiteral("h")
                                                  : QStringLiteral("w"))
                            .toInt() + spacing;
                    offset += qMax(step, 1);
                }
                // Stash the per-entry step + entry count so the click
                // handler can clamp cb_prev/nextpage scrolling and
                // TreePainter can compute the scroll offset for the
                // child translate.
                // Route through setXmlParam virtual so
                // ComponentBucketWidget can keep its typed `_scroll`
                // / `_entry_step` shadow in sync with the attrs hash.
                if (step > 0)
                    node.setXmlParam(QStringLiteral("_entry_step"),
                                      QString::number(step));
                node.setXmlParam(QStringLiteral("_entry_count"),
                                  QString::number(ids.size()));
            }
        }
        parent.children.push_back(std::move(nodePtr));
    }

    void applySendparams(ResolvedWidget &w, const QString &instanceId) {
        if (w.id.isEmpty()) return;
        if (m_hidden.contains(w.id)) {
            w.attrs.insert(QStringLiteral("visible"), QStringLiteral("0"));
        }
        // Force certain widgets visible by default — these have
        // visible="0" in their XML instances but real Wasabi's
        // videoavs.m onShowVis() shows them after a Timer-backed
        // callback chain that our Maki port doesn't fully drive.
        // Forcing visible="1" here lets the user see the vis/video
        // detach button when the AVS drawer is opened.  Same fix
        // pattern as `kScriptHiddenByDefault` (a few lines above)
        // but in the opposite direction.
        static const QSet<QString> kForceVisibleByDefault = {
            QStringLiteral("buttons.vis.detach"),
            QStringLiteral("buttons.video.detach"),
        };
        if (kForceVisibleByDefault.contains(w.id)) {
            w.attrs.insert(QStringLiteral("visible"), QStringLiteral("1"));
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
    static bool replaceById(Widget &node, const QString &targetId,
                            std::vector<std::unique_ptr<Widget>> &replacement) {
        for (auto &c : node.children) {
            if (!c) continue;
            if (c->id == targetId) {
                c->children = std::move(replacement);
                return true;
            }
            if (replaceById(*c, targetId, replacement)) return true;
        }
        return false;
    }

    const GroupdefIndex &m_groupdefs;
    const SendparamsMap &m_sendparams;
    const QSet<QString> &m_hidden;
    QSet<QString>        m_inflightInstances;
};

void resolveGroupXFadePagesRec(ResolvedWidget &node,
                                const SkinXml::Document &doc,
                                const GroupdefIndex &index,
                                const SendparamsMap &sendparams,
                                const QSet<QString> &hidden) {
    if (node.tag == QStringLiteral("groupxfade")) {
        const QString gid = node.attrs.value(QStringLiteral("groupid"));
        const QString cached =
            node.attrs.value(QStringLiteral("_resolved_groupid"));
        if (!gid.isEmpty() && gid != cached) {
            // Drop any previously-materialised page and re-expand the
            // newly-named groupdef into our children.
            node.children.clear();
            if (const Element *def = index.lookup(gid)) {
                Expander ex(index, sendparams, hidden);
                ex.expandChildren(*def, node, /*instanceId=*/{});
            }
            node.attrs.insert(QStringLiteral("_resolved_groupid"), gid);
        }
    }
    for (auto &c : node.children)
        if (c) resolveGroupXFadePagesRec(*c, doc, index, sendparams, hidden);
}

}  // namespace

void resolveGroupXFadePages(ResolvedWidget &root,
                            const SkinXml::Document &doc) {
    GroupdefIndex index;
    collectGroupdefs(doc.root, index);
    SendparamsMap sendparams;
    collectSendparams(doc.root, sendparams);
    QSet<QString> hidden;
    collectHideObjects(doc.root, hidden);
    resolveGroupXFadePagesRec(root, doc, index, sendparams, hidden);
}

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

    // Populate `out` (the caller's root Widget) with the layout's tag/
    // attrs/source info, then expand its children.  We can't replace
    // `out` wholesale via assignment because Widget is polymorphic
    // (and the caller owns it); we just copy the fields and recurse.
    out.tag        = layout->tag;
    out.id         = layout->attrs.value(QStringLiteral("id"));
    out.instanceId = layout->attrs.value(QStringLiteral("instanceid"));
    out.attrs      = layout->attrs;
    out.sourceFile = layout->sourceFile;
    out.sourceLine = layout->sourceLine;
    out.children.clear();
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
        if (c) if (auto *r = findById(*c, id)) return r;
    return nullptr;
}

ResolvedWidget *findByTag(ResolvedWidget &w, const QString &tag) {
    if (w.tag == tag) return &w;
    for (auto &c : w.children)
        if (c) if (auto *r = findByTag(*c, tag)) return r;
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
            for (auto &c : w.children) if (c) walk(*c);
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
    // applies (StandardBindings.cpp's getAutoWidth, +11 total).
    // Without these, the streak gap math runs against a smaller
    // text width than is actually painted and the right streak
    // overlaps the title text.
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
            if (ok && fontsize > 0) {
                int rn = 6, rd = 7;
                if (const char *r = ::getenv("WASABIQT_FONT_RATIO")) {
                    int a = 0, b = 0;
                    if (sscanf(r, "%d,%d", &a, &b) == 2 && a > 0 && b > 0) {
                        rn = a; rd = b;
                    }
                }
                f.setPixelSize(qMax(1, (fontsize * rn + rd/2) / rd));
            }
            if (title->attrs.value(QStringLiteral("bold")) ==
                QStringLiteral("1"))
                f.setBold(true);
            QFontMetrics fm(f);
            textWidth = fm.horizontalAdvance(s) + 11;
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
            if (c) walk(*c, childOffset, newPadLeft, newPadRight);
    };
    walk(root, 0, 0, 0);
}

}  // namespace knownscripts

void runKnownScripts(ResolvedWidget &root, int layoutWidth) {
    knownscripts::applyTo(root, layoutWidth);

    // (The `player.normal.drawer.content` centring hardcode used to
    // live here as a static substitute for configtabs.m's onResize
    // handler.  It's gone now — fixing SOM::makeInt/makeFloat/
    // makeDouble to be type-aware unblocked the real Maki bytecode
    // path: `w/2 - 163` now correctly evaluates to 14 inside the
    // VM and setXmlParam("x", "14") lands on drawer.content via the
    // dispatchInitialResize chain.  Embedders that prefer not to
    // fire onResize at load time can leave dispatchInitialResize off
    // and the drawer stays uncentered, but the static hardcode is
    // no longer needed for either path.)

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
                        if (c) if (auto *r = findById(*c)) return r;
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
        for (auto &c : w.children) if (c) resolveAutoWidth(*c);
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

// Geometry resolution shared with the painter / hit-tester:
// Widget::resolveRectFromAttrs (single source of truth for
// relatx / relaty / relatw / relath / fitparent semantics).
//
// Used below by paintRegionLayers when computing per-widget bbox
// for sysregion contributions.

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
    HitCtx ctx;
    ctx.actionOnly = actionOnly;
    if (imageSize) {
        ctx.imageSize = [imageSize, imageSizeUserdata](const QString &id) {
            return imageSize(id, imageSizeUserdata);
        };
    }
    return const_cast<Widget &>(root).hitTest(
        pointInLayout, QPoint(0, 0), rootCanvas, ctx, outBbox);
}

// ── Window-region builder ─────────────────────────────────────────
namespace {
// Walk the tree, painting only sysregion-tagged layers onto `out`.
// Wasabi convention: any layer with `sysregion` != 0 contributes
// its opaque pixels to the window region.  The actual numeric value
// (1, -2, …) controls subtleties like alpha-edge anti-aliasing that
// we don't yet implement — for now we treat all non-zero values
// the same way (opaque pixels = part of region).
// Phase: additive (sysregion="1" fillRects + layers) or
// subtractive (sysregion="-N" cutouts).  paintRegionLayers walks
// the tree twice: first pass marks the region, second pass cuts.
// Without the two-pass split, a cutout inside one container could
// be overwritten by a subsequent sibling container's fillRect (the
// drawer's left-edge cutout vs player.main's fillRect — both walk
// inside player.content.dummy.group, drawer first, so drawer
// cutouts got re-filled by player.main).
enum class RegionPass { Additive, Subtractive };

// Sysregion-bitmap "narrowing-strip" detector.
//
// Modern's drawer ships its left/right edge masks as 20×129 bitmaps with
// the leftmost (or rightmost) 8 columns opaque for the FULL height plus
// a tiny 5-row staircase widening at the bottom.  Read as a cutout, that
// bitmap encodes TWO things at once:
//
//   • A constant N-col-wide "narrowing strip" along the long edge — for
//     the wl_surface input region, this is correct: it marks the strip
//     as outside the drawer's clickable area so the desktop catches
//     clicks there.
//
//   • A bottom-corner staircase shape — this is the rounded-corner
//     visual that the chrome should actually display.
//
// When we replay this mask onto the chrome buffer with DestinationOut
// for VISUAL shaping (paintRegionCutouts, not computeWindowRegion), the
// narrowing strip cuts into pixels painted by SIBLING parent groups
// further up — concretely, the player chrome painted at canvas y where
// the drawer cutout's tall bitmap also overlays.  The result is the
// "two white rectangles between player and drawer" notch the user
// reported: the drawer's full-height cut clips through the player.
//
// The fix: for tall cutout bitmaps (h/w > 2), strip out any column that
// is opaque across the ENTIRE bitmap height.  Those columns are the
// narrowing-strip — they're shape information meant for the OS input
// region, not for the visual chrome.  Columns that vary along the
// height stay — those are the actual corner staircase the visual needs.
//
// Short bitmaps (player.main.left.region's 6×6 corner mask,
// wasabi.frame.top.left.region's 10×18 top-corner staircase) keep their
// full content — they're entirely "varying-column" corner shapes with
// no narrowing-strip component to remove.
QImage stripNarrowingColumns(const QImage &src) {
    const int W = src.width();
    const int H = src.height();
    if (W == 0 || H == 0) return src;
    // Only apply the strip filter to bitmaps where height >> width — that's
    // the shape signature of a narrowing-strip mask (tall, with a tiny
    // varying corner at the bottom).  Square / short bitmaps are entire-
    // corner masks and must pass through unchanged.
    if (H <= W * 2) return src;
    QImage out = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int x = 0; x < W; ++x) {
        bool allOpaque = true;
        for (int y = 0; y < H; ++y) {
            if (qAlpha(out.pixel(x, y)) == 0) { allOpaque = false; break; }
        }
        if (allOpaque) {
            for (int y = 0; y < H; ++y) out.setPixel(x, y, 0u);
        }
    }
    return out;
}

void paintRegionLayers(QPainter &p, const ResolvedWidget &w,
                       BitmapRegistry &reg, QSize canvas,
                       bool &outFoundAny,
                       RegionPass pass,
                       bool stripNarrowing = false) {
    if (w.attrs.value(QStringLiteral("visible")) ==
        QStringLiteral("0")) {
        return;
    }

    const QRect r = Widget::resolveRectFromAttrs(w.attrs, canvas);
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
        if (sr == QStringLiteral("1") && pass == RegionPass::Additive) {
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
            const bool runHere =
                cutoutMode ? (pass == RegionPass::Subtractive)
                           : (pass == RegionPass::Additive);
            if (runHere) {
                if (cutoutMode)
                    p.setCompositionMode(
                        QPainter::CompositionMode_DestinationOut);
                if (cutoutMode && stripNarrowing) {
                    // Visual cutout path: strip narrowing-strip cols so
                    // the cutout only contributes its varying corner
                    // shape, not the full-height inset.  Build the rect
                    // with paintLayer's resolution rules but draw the
                    // filtered bitmap ourselves.
                    QImage src = reg.imageFor(img);
                    QImage filtered = stripNarrowingColumns(src);
                    int x = w.attrs.value(QStringLiteral("x")).toInt();
                    int y = w.attrs.value(QStringLiteral("y")).toInt();
                    int wp = w.attrs.value(QStringLiteral("w")).toInt();
                    int hp = w.attrs.value(QStringLiteral("h")).toInt();
                    if (w.attrs.value(QStringLiteral("relatx")) ==
                        QStringLiteral("1"))
                        x = canvas.width()  + x;
                    if (w.attrs.value(QStringLiteral("relaty")) ==
                        QStringLiteral("1"))
                        y = canvas.height() + y;
                    if (w.attrs.value(QStringLiteral("relatw")) ==
                        QStringLiteral("1"))
                        wp = canvas.width()  + wp;
                    if (w.attrs.value(QStringLiteral("relath")) ==
                        QStringLiteral("1"))
                        hp = canvas.height() + hp;
                    if (wp <= 0) wp = filtered.width();
                    if (hp <= 0) hp = filtered.height();
                    if (!filtered.isNull())
                        p.drawImage(QRect(x, y, wp, hp), filtered);
                } else {
                    LayerPainter::paintLayer(&p, reg, w.attrs, canvas);
                }
                if (cutoutMode)
                    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
                outFoundAny = true;
            }
        }
    }

    for (const auto &c : w.children)
        if (c) paintRegionLayers(p, *c, reg, childCanvas, outFoundAny, pass,
                                  stripNarrowing);

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
        // Two-pass walk so cutouts (sysregion="-1" / "-2") run AFTER
        // all additive regions are marked.  Single-pass walks would
        // let a sibling's fillRect overwrite a prior cutout — that
        // bug was the source of the "white rectangles on the left/
        // right of the drawer" visual: drawer's edge cutouts ran
        // first, then player.main's fillRect re-marked those pixels
        // as in-region, defeating the cutout.
        paintRegionLayers(p, root, registry, canvas, foundAny,
                          RegionPass::Additive);
        paintRegionLayers(p, root, registry, canvas, foundAny,
                          RegionPass::Subtractive);
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

namespace {

void collectChromeCutoutsRec(
        const ResolvedWidget &node,
        QHash<QString, QList<ChromeCutout>> &out) {
    // For each child of this node, if it's a `sysregion="-N"` cutout
    // layer, look for a sibling chrome layer whose image is the
    // cutout's image minus the ".region" suffix.  Record the cutout
    // with its offset relative to the chrome (in parent-local coords).
    const QString kRegionSuffix = QStringLiteral(".region");
    for (const auto &cutoutPtr : node.children) {
        if (!cutoutPtr) continue;
        const Widget &cutout = *cutoutPtr;
        if (cutout.tag != QStringLiteral("layer")) continue;
        const QString sr = cutout.attrs.value(QStringLiteral("sysregion"));
        if (sr.isEmpty() || !sr.startsWith(QChar('-'))) continue;
        const QString cutoutImg = cutout.attrs.value(QStringLiteral("image"));
        if (!cutoutImg.endsWith(kRegionSuffix)) continue;
        const QString chromeImg =
            cutoutImg.left(cutoutImg.size() - kRegionSuffix.size());
        for (const auto &chromePtr : node.children) {
            if (!chromePtr) continue;
            const Widget &chrome = *chromePtr;
            if (chrome.tag != QStringLiteral("layer")) continue;
            if (chrome.attrs.value(QStringLiteral("image")) != chromeImg)
                continue;
            int cx = chrome.attrs.value(QStringLiteral("x")).toInt();
            int cy = chrome.attrs.value(QStringLiteral("y")).toInt();
            int rx = cutout.attrs.value(QStringLiteral("x")).toInt();
            int ry = cutout.attrs.value(QStringLiteral("y")).toInt();
            ChromeCutout cc;
            cc.cutoutImage = cutoutImg;
            cc.offset      = QPoint(rx - cx, ry - cy);
            out[chromeImg].append(cc);
            break;
        }
    }
    for (const auto &child : node.children)
        if (child) collectChromeCutoutsRec(*child, out);
}

}  // namespace

QHash<QString, QList<ChromeCutout>>
collectChromeCutouts(const ResolvedWidget &root) {
    QHash<QString, QList<ChromeCutout>> out;
    collectChromeCutoutsRec(root, out);
    if (::getenv("WASABIQT_TRACE_CUTOUT_PAIRS")) {
        for (auto it = out.constBegin(); it != out.constEnd(); ++it) {
            for (const ChromeCutout &cc : it.value()) {
                fprintf(stderr,
                  "[cutout-pair] chrome=%s cutout=%s offset=(%d,%d)\n",
                  it.key().toLocal8Bit().constData(),
                  cc.cutoutImage.toLocal8Bit().constData(),
                  cc.offset.x(), cc.offset.y());
            }
        }
    }
    return out;
}

void paintRegionCutouts(QPainter &p, const ResolvedWidget &root,
                        BitmapRegistry &registry, QSize canvas) {
    if (canvas.width() <= 0 || canvas.height() <= 0) return;
    bool foundAny = false;
    const bool savedAA = p.testRenderHint(QPainter::Antialiasing);
    const bool savedSP = p.testRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::Antialiasing,          false);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    if (::getenv("WASABIQT_TRACE_CUTOUTS"))
        fprintf(stderr, "[cutouts] >>> paintRegionCutouts canvas=%dx%d\n",
                canvas.width(), canvas.height());
    paintRegionLayers(p, root, registry, canvas, foundAny,
                      RegionPass::Subtractive,
                      /*stripNarrowing=*/true);
    if (::getenv("WASABIQT_TRACE_CUTOUTS"))
        fprintf(stderr, "[cutouts] <<< paintRegionCutouts foundAny=%d\n",
                foundAny ? 1 : 0);
    p.setRenderHint(QPainter::Antialiasing,          savedAA);
    p.setRenderHint(QPainter::SmoothPixmapTransform, savedSP);
}

}  // namespace WasabiQt::Layout
