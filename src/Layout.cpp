// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <qtWasabi/CfgAttribStore.h>
#include <qtWasabi/HitCtx.h>
#include <qtWasabi/Layout.h>
#include <qtWasabi/TextPainter.h>
#include <qtWasabi/SkinXml.h>
#include <qtWasabi/BitmapRegistry.h>
#include <qtWasabi/LayerPainter.h>
#include <qtWasabi/Widget.h>

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

namespace qtWasabi::Layout {

namespace {

using SkinXml::Element;

// (instanceId, targetWidgetId) -> {attr: value}
using SendparamsMap = QHash<QPair<QString, QString>, QHash<QString, QString>>;

// Index groupdefs by their `id` AND by their `xuitag` (lowercased,
// colons replaced with underscores to match the normalisation that
// SkinXml::parse applies to element names).  Same node can appear
// under both keys.
struct GroupdefIndex {
    // Both maps store keys in lowercase — groupdef ids are matched
    // case-insensitively (Bento defines `id="info.component.Cover"`
    // but instantiates as `id="info.component.cover"`).  Lookup also
    // lowercases before searching.
    QHash<QString, const Element *> byId;
    QHash<QString, const Element *> byXuitag;

    const Element *lookup(const QString &key) const {
        const QString k = key.toLower();
        if (auto *p = byId.value(k, nullptr))      return p;
        if (auto *p = byXuitag.value(k, nullptr))  return p;
        return nullptr;
    }
};

void collectGroupdefs(const Element &el, GroupdefIndex &out) {
    if (el.tag == QStringLiteral("groupdef")) {
        const QString id = el.attrs.value(QStringLiteral("id"));
        if (!id.isEmpty()) out.byId.insert(id.toLower(), &el);
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
            c.attrs.value(QStringLiteral("id"))
                .compare(id, Qt::CaseInsensitive) == 0)
            return &c;
    }
    return nullptr;
}

// A container's first <layout> child is its default — what Winamp shows
// when no specific layout is requested.  Skins are free to name their
// default anything ("normal" is only a convention, e.g. HeadAMP uses
// "mode-main"), so callers that ask for a conventional name fall back
// here rather than failing.
const Element *firstLayout(const Element &container) {
    for (const auto &c : container.children) {
        if (c.tag == QStringLiteral("layout"))
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
    // Bulk assignment skipped setXmlParam — fire the one-shot init
    // hook so subclasses can register attr-driven side effects (e.g.
    // ToggleButton subscribing to its cfgattrib key on first sight).
    r->onAttrsInitialized();
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
            // `embed_xui` most often names a CHILD element id in the
            // groupdef body — the embedding point (TabButton uses
            // embed_xui="bento.tabbutton.mousetrap", its inner button).
            // Such a groupdef is a structured CONTAINER and must render
            // its whole body, NOT collapse to a single leaf.  This also
            // covers Bento's InfoLine, whose embed_xui="text" names its
            // child <Text id="text"> (the value field) — the literal
            // "text" only LOOKS like the basic tag; treating it as a
            // leaf dropped the label/value children.  Only when
            // embed_xui names a basic widget tag AND no child carries
            // that id is the groupdef a bare embedded widget.
            std::function<bool(const Element &, const QString &)>
                hasChildId = [&](const Element &e, const QString &id) -> bool {
                    for (const Element &c : e.children) {
                        if (c.attrs.value(QStringLiteral("id")) == id) return true;
                        if (hasChildId(c, id)) return true;
                    }
                    return false;
                };
            const bool embedNamesChild =
                !embedXui.isEmpty() && hasChildId(*def, embedXui);
            if (!embedNamesChild &&
                kBasicXuiTags.contains(embedXui.toLower())) {
                finalTag = embedXui.toLower();
            } else if (finalTag != QStringLiteral("group") &&
                       finalTag != QStringLiteral("container") &&
                       finalTag != QStringLiteral("layout")) {
                finalTag = QStringLiteral("group");
            }

            auto nodePtr = makeResolved(el, finalTag);
            Widget &node = *nodePtr;

            // Tag the instance with the groupdef it came from.  A
            // <script> declared inside a groupdef body is parsed ONCE
            // (scope = the groupdef id), so a groupdef instantiated N
            // times — Bento's InfoLine is instantiated per file-info
            // line — shares a single script instance bound to one line.
            // SkinRuntime uses this tag to give each instance its OWN
            // script (Wasabi semantics) so per-instance handlers (e.g.
            // infoline.maki positioning each value after its label) run.
            {
                const QString gdId = def->attrs.value(QStringLiteral("id"));
                if (!gdId.isEmpty())
                    node.attrs.insert(QStringLiteral("_srcgroupdef"), gdId);
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

            // A groupdef `background="X"` fills the group rect with bitmap X
            // BEHIND its children (Wasabi semantics).  The menu bar relies on
            // it: menubar.png's bottom row is transparent, so without the fill
            // that row exposes the desktop — the 1px gap under the menu bar.
            //
            // SCOPE: only the menu bar needs this.  Window frames and the EQ
            // drawer also declare background=, but their texture sits behind
            // content that fully covers it — EXCEPT when a frame pane resolves
            // to a sliver (the EQ-expanded layout grows a thin pane at the
            // right edge), where the fill paints a spurious vertical
            // basetexture strip against the desktop.  Before this fill that
            // sliver was transparent and invisible; filling every group made
            // it visible.  Restrict the fill to menu-bar groups: keeps the gap
            // fix, drops the strip.
            const QString bgImage =
                node.attrs.value(QStringLiteral("background"));
            // Reference semantics: background= is the rearmost fill of
            // EVERY group (skins like winamp1 paint their entire window
            // face this way).  The earlier menubar-only restriction
            // papered over sliver frame panes becoming visible; the
            // painter now skips a __groupbg whose resolved rect is a
            // sliver instead, so a degenerate pane stays invisible
            // without robbing every other group of its background.
            if (!bgImage.isEmpty()) {
                Element bgEl;
                bgEl.tag = QStringLiteral("layer");
                bgEl.attrs.insert(QStringLiteral("id"),
                                  QStringLiteral("__groupbg"));
                bgEl.attrs.insert(QStringLiteral("image"), bgImage);
                bgEl.attrs.insert(QStringLiteral("x"), QStringLiteral("0"));
                bgEl.attrs.insert(QStringLiteral("y"), QStringLiteral("0"));
                bgEl.attrs.insert(QStringLiteral("w"), QStringLiteral("0"));
                bgEl.attrs.insert(QStringLiteral("h"), QStringLiteral("0"));
                bgEl.attrs.insert(QStringLiteral("relatw"),
                                  QStringLiteral("1"));
                bgEl.attrs.insert(QStringLiteral("relath"),
                                  QStringLiteral("1"));
                bgEl.attrs.insert(QStringLiteral("tile"),
                                  QStringLiteral("1"));
                node.children.insert(node.children.begin(),
                                     makeResolved(bgEl));
            }

            // The standardframe menu bar sits 1px above a sunken content
            // area.  `menubar.png`'s bottom row is transparent, and real
            // Winamp shows a 1px shadow at that boundary (the content-area
            // top inset edge), NOT the bare frame texture.  Our background
            // fill paints the texture through that transparent row, leaving
            // a too-light line where the reference has a dark divider.
            // Reproduce the divider as a 1px ~40%-black line at the menu
            // bar's bottom edge — a solid-colour layer drawn last (on top),
            // themed by whatever sits beneath it (silver here, darker on a
            // dark skin).  Structural: any standardframe `*menubar*` group.
            if (node.id.contains(QStringLiteral("menubar"),
                                 Qt::CaseInsensitive)) {
                Element sh;
                sh.tag = QStringLiteral("layer");
                sh.attrs.insert(QStringLiteral("id"),
                                QStringLiteral("__menubar_divider"));
                sh.attrs.insert(QStringLiteral("file"),
                                QStringLiteral("$solid"));
                sh.attrs.insert(QStringLiteral("color"),
                                QStringLiteral("0,0,0"));
                sh.attrs.insert(QStringLiteral("alpha"),
                                QStringLiteral("98"));
                sh.attrs.insert(QStringLiteral("x"), QStringLiteral("0"));
                sh.attrs.insert(QStringLiteral("y"), QStringLiteral("-1"));
                sh.attrs.insert(QStringLiteral("relaty"), QStringLiteral("1"));
                // Span the menu strip (menubar.left + center); the right cap
                // (menubar.right, 45px) keeps its own bright bottom edge.
                sh.attrs.insert(QStringLiteral("w"), QStringLiteral("-45"));
                sh.attrs.insert(QStringLiteral("relatw"), QStringLiteral("1"));
                sh.attrs.insert(QStringLiteral("h"), QStringLiteral("1"));
                node.children.push_back(makeResolved(sh));
            }

            // XUI text-param propagation.  Real Wasabi skins use
            // per-instance "label" attrs on XUI groupdef tags
            // (Bento's `<Bento:TabButton tabtext="Media Library">`,
            // WACUP's `<Wasabi:TabSheet caption="Options">`, etc.)
            // and a Maki script does `text.setXmlParam("text", …)`
            // to flow the value into the inner `<text>` widget.  We
            // can't run those scripts; instead the engine recognises
            // the canonical XUI text-param names below and writes
            // them as `text=…` on any inner `<text>` widget whose
            // own display/default/text attrs are all empty.  Skin-
            // agnostic for any skin using one of these param names.
            static const QStringList kXuiTextParamNames{
                QStringLiteral("tabtext"),
                QStringLiteral("caption"),
                QStringLiteral("label"),
            };
            QString xuiTextValue, xuiTextParam;
            for (const QString &k : kXuiTextParamNames) {
                const QString v = el.attrs.value(k);
                if (!v.isEmpty()) { xuiTextValue = v; xuiTextParam = k; break; }
            }
            if (!xuiTextValue.isEmpty()) {
                // Prefer a child whose id matches the param name: Bento's
                // InfoLine declares `label="Title:"` and a child
                // `<Text id="label">`, so the label belongs on that
                // child — NOT on the sibling `<Text id="text">` (the
                // value field, filled at runtime by fileinfo.maki).
                // Setting it there (and only there) keeps the value
                // field free.  Falls back to tagging every empty text
                // descendant for the simple case (TabButton: `tabtext`
                // → its normal/hover/active text widgets, no id="tabtext").
                std::function<Widget *(Widget &, const QString &)> findById =
                    [&](Widget &w, const QString &id) -> Widget * {
                        for (auto &c : w.children) {
                            if (!c) continue;
                            if (c->attrs.value(QStringLiteral("id")) == id &&
                                (c->tag == QStringLiteral("text") ||
                                 c->tag == QStringLiteral("songticker")))
                                return c.get();
                            if (Widget *r = findById(*c, id)) return r;
                        }
                        return nullptr;
                    };
                Widget *named = findById(node, xuiTextParam);
                if (named) {
                    named->attrs.insert(QStringLiteral("text"), xuiTextValue);
                } else {
                    std::function<void(Widget &)> tagTexts =
                        [&](Widget &w) {
                            if (w.tag == QStringLiteral("text") ||
                                w.tag == QStringLiteral("songticker")) {
                                const bool empty =
                                    w.attrs.value(QStringLiteral("text")).isEmpty() &&
                                    w.attrs.value(QStringLiteral("default")).isEmpty() &&
                                    w.attrs.value(QStringLiteral("display")).isEmpty();
                                if (empty) {
                                    w.attrs.insert(QStringLiteral("text"),
                                                    xuiTextValue);
                                }
                            }
                            for (auto &c : w.children) {
                                if (c) tagTexts(*c);
                            }
                        };
                    tagTexts(node);
                }
            }

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
                // The content groupdef's children are flattened below with
                // no wrapper node, which would erase the groupdef identity
                // — record it so script-owner resolution (a <script>
                // declared INSIDE the content groupdef, e.g. the pledit's
                // plmenu/pltime) can still find its owner in this tree.
                node.attrs.insert(QStringLiteral("_content_groupdef"),
                                  content);
                if (const Element *contentDef = m_groupdefs.lookup(content)) {
                    if (!m_inflightInstances.contains(content)) {
                        m_inflightInstances.insert(content);
                        const QString embedTarget =
                            def->attrs.value(QStringLiteral("embed_xui"));
                        // Build the content's children into a temp node...
                        Widget contentExp;
                        expandChildren(*contentDef, contentExp,
                                       iid.isEmpty() ? instanceId : iid);
                        // Append content at the end (natural XML
                        // order).  Player chrome groups (player.main,
                        // drawer, AVSGroup) carry `_shift_y=18` (set
                        // below) so they paint at y=35+ and never
                        // collide with the menubar at y=18..35.
                        // Sibling overlay widgets the content authors
                        // placed in the menubar band (WinampModernPP's
                        // videoavs.open / openclosehider, literal
                        // y=17..19) paint last and stay on top of
                        // the menubar background.
                        if (!embedTarget.isEmpty()) {
                            replaceById(node, embedTarget, contentExp.children);
                        } else {
                            for (auto &c : contentExp.children) {
                                node.children.push_back(std::move(c));
                            }
                        }
                        m_inflightInstances.remove(content);

                        // mainmenuoverlay.maki equivalent: at runtime
                        // the script brings the menubar's text /
                        // overlay layers to front so they paint over
                        // any chrome that the content widgets placed
                        // in the menubar's right region (the
                        // WinampModernPP VIDEO/VIS pill).  Without the
                        // script running we'd otherwise see the pill
                        // covering the "VIDEO/VIS" text.  Replicate
                        // statically: find any layer inside a menubar
                        // child whose id ends in `.textoverlay` (or
                        // contains the same), move it out to the
                        // MainFrame level at the end, and add the
                        // menubar's y to compensate for the lost
                        // parent translate.
                        for (auto &mb : node.children) {
                            if (!mb || !mb->id.contains(
                                    QStringLiteral("menubar"),
                                    Qt::CaseInsensitive))
                                continue;
                            const int mbY = mb->attrs.value(
                                QStringLiteral("y")).toInt();
                            auto &mbk = mb->children;
                            for (auto it = mbk.begin(); it != mbk.end();) {
                                auto &child = *it;
                                if (child && child->id.contains(
                                        QStringLiteral("textoverlay"),
                                        Qt::CaseInsensitive)) {
                                    const int curY = child->attrs.value(
                                        QStringLiteral("y")).toInt();
                                    child->setXmlParam(
                                        QStringLiteral("y"),
                                        QString::number(curY + mbY));
                                    node.children.push_back(
                                        std::move(child));
                                    it = mbk.erase(it);
                                } else {
                                    ++it;
                                }
                            }
                        }
                    }
                }

                // Shift the injected content down so it clears the
                // titlebar (and the menubar, where one exists).  The
                // standardframe.maki script places the content group at
                // the y named in its `param` token 1 — "18" for every
                // player-window frame (the titlebar height), so the
                // component never paints over the title.  The shift is
                // INDEPENDENT of the menubar: the detached Visualizer /
                // Video frames carry NO menubar, yet their content still
                // has to sit below the titlebar.  Each per-frame dummy
                // group already bakes its own menubar clearance into its
                // y (17 for the Playlist/Media Library, 0 for vis/video),
                // so a single titlebar-height shift is correct for all.
                //
                // The shift is applied *at resolve-time* via a private
                // `_shift_y` attr that Widget::resolveRectFromAttrs
                // adds to the resolved y.  We can't bake the shift into
                // the `y` attr itself — drawer.m / configtabs.m later
                // overwrite that attr with hardcoded values (e.g.
                // setXmlParam(player.normal.drawer, y=-147) during the
                // open-drawer animation), and those values were
                // authored for the unshifted layout.  Keeping `y`
                // pristine and applying the shift at the resolve step
                // means script-set positions get shifted too.
                //
                // Read the offset from the frame's standardframe script
                // param (token 1) so non-18 frames (config StandardFrames
                // place content at y=23) shift by their own value.
                int contentShiftY = 18;
                for (const Element &dc : def->children) {
                    if (dc.tag != QStringLiteral("script")) continue;
                    const QStringList toks =
                        dc.attrs.value(QStringLiteral("param"))
                            .split(QLatin1Char(','));
                    if (toks.size() < 8) continue;
                    bool ok = false;
                    const int yTok = toks.at(1).trimmed().toInt(&ok);
                    if (ok) { contentShiftY = yTok; break; }
                }
                // The menubar (where present) sits at its own y already and
                // must not be shifted; locate it only to exempt it.
                Widget *menubarChild = nullptr;
                for (auto &c : node.children) {
                    if (c && c->id.contains(QStringLiteral("menubar"),
                                            Qt::CaseInsensitive)) {
                        menubarChild = c.get();
                        break;
                    }
                }
                const QString shiftStr = QString::number(contentShiftY);
                for (auto &c : node.children) {
                    if (!c) continue;
                    if (c.get() == menubarChild) continue;
                    // The frame's content wrapper carries its OWN titlebar
                    // at y=0; it must NOT be pushed down.  The player frame
                    // uses `wasabi.main.layout`; the Playlist Editor / Media
                    // Library / Visualizer / Video frames use
                    // `wasabi.standard.layout` — exempt both, else those
                    // windows' titlebars render low (behind the content).
                    if (c->id == QStringLiteral("wasabi.main.layout") ||
                        c->id == QStringLiteral("wasabi.standard.layout"))
                        continue;
                    if (c->tag == QStringLiteral("script") ||
                        c->tag == QStringLiteral("sendparams"))
                        continue;
                    for (auto &gc : c->children) {
                        if (!gc) continue;
                        // Shift every content-level child by the titlebar
                        // height — player chrome groups AND sibling overlay
                        // widgets the authors placed at the top of
                        // player.main (player.button.videoavs.up.bg,
                        // videoavs.open / videoavs.close, openclosehider —
                        // literal y=16..19).  In the WACUP reference these
                        // sit in row 3 (below the y=18..35 menubar row),
                        // not in the menubar.  The "VIDEO/VIS" text label
                        // that DOES live in the menubar band paints from the
                        // menubar's own `textoverlay` layer (hoisted above).
                        gc->attrs.insert(
                            QStringLiteral("_shift_y"), shiftStr);
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
            // The XML's `width=` is the initial divider position.
            // Real Wasabi restores the user's saved divider from
            // skin state on subsequent loads, but on the very first
            // launch this literal value is what the chrome shows.
            // Honour it — our previous heuristic that expanded to
            // `maxwidth` made the player chrome look stretched (the
            // reference Bento sits at the saved-position ~290 px
            // on a 730 px window, much closer to the XML literal
            // than to maxwidth).
            int defaultSize = vertical
                ? el.attrs.value(QStringLiteral("width")).toInt()
                : el.attrs.value(QStringLiteral("height")).toInt();

            // Which edge is the divider position (`width`/`height`)
            // measured from?  Wasabi FrameWnd: from=l/top anchors the
            // fixed size to the FIRST pane (left/top); from=r/bottom
            // anchors it to the SECOND pane (right/bottom) and gives
            // the first pane the remainder.  The old code ignored
            // `from` entirely and always sized the first pane, which
            // swapped Bento's player.dualwnd (from="r", width=200):
            // fileinfo wrongly got 200px and the playlist preview got
            // the leftover — producing the swapped/empty top-right
            // pane.  This is attribute-driven, no skin names.
            const QString fromAttr =
                el.attrs.value(QStringLiteral("from")).toLower();
            const bool fixedOnSecond =
                (fromAttr == QStringLiteral("r") ||
                 fromAttr == QStringLiteral("right") ||
                 fromAttr == QStringLiteral("b") ||
                 fromAttr == QStringLiteral("bottom"));

            // Wasabi FrameWnd reserves an 8px sizer gap between the panes
            // (the SIZERWIDTH) for the draggable divider/grabber — but
            // ONLY when the frame is resizeable (a non-resizeable frame
            // zeroes the sizer width).  Non-resizeable frames
            // (jump="0"/"none") butt their panes together with no gap and
            // no divider.  `jump` set to a real edge ⇒ draggable.
            const QString jumpAttr =
                el.attrs.value(QStringLiteral("jump")).toLower();
            const bool resizeable =
                !(jumpAttr == QStringLiteral("0") ||
                  jumpAttr == QStringLiteral("none") ||
                  jumpAttr == QStringLiteral("false"));
            int kFrameSizerWidth = resizeable ? 8 : 0;
            if (const char *sz = qgetenv("WASABIQT_SIZER").constData();
                sz && *sz) kFrameSizerWidth = QByteArray(sz).toInt();

            auto addPane = [&](const QString &paneId, bool isFirst) {
                if (paneId.isEmpty()) return;
                if (qEnvironmentVariableIntValue("WASABIQT_TRACE_FRAME") == 1)
                    fprintf(stderr, "[frame %s] pane id=%s isFirst=%d lookup=%d\n",
                            el.attrs.value(QStringLiteral("id")).toLocal8Bit().constData(),
                            paneId.toLocal8Bit().constData(), isFirst ? 1 : 0,
                            m_groupdefs.lookup(paneId) ? 1 : 0);
                if (!m_groupdefs.lookup(paneId)) return;
                Element pseudo;
                pseudo.tag = QStringLiteral("group");
                pseudo.attrs.insert(QStringLiteral("id"), paneId);
                if (defaultSize > 0) {
                    // Does THIS pane get the fixed size or the
                    // remainder?  The fixed pane is the first one
                    // UNLESS `from` points at the far edge.
                    const bool fixedHere = (isFirst != fixedOnSecond);
                    // Tag the pane's role so a runtime setPosition() can
                    // re-split the frame against the live divider (faithful
                    // FrameWnd model: getPosition/setPosition drive a live
                    // pullbarpos, not the static `width=`).
                    pseudo.attrs.insert(QStringLiteral("_frame_pane"),
                        fixedHere ? QStringLiteral("fixed") : QStringLiteral("rem"));
                    // The cross-axis always fills the parent.
                    const char *crossPos = vertical ? "y" : "x";
                    const char *crossSz  = vertical ? "h" : "w";
                    const char *crossRel = vertical ? "relath" : "relatw";
                    const char *mainPos  = vertical ? "x" : "y";
                    const char *mainSz   = vertical ? "w" : "h";
                    const char *mainRel  = vertical ? "relatw" : "relath";
                    const char *mainRelPos = vertical ? "relatx" : "relaty";
                    pseudo.attrs.insert(QString::fromLatin1(crossPos), QStringLiteral("0"));
                    pseudo.attrs.insert(QString::fromLatin1(crossSz),  QStringLiteral("0"));
                    pseudo.attrs.insert(QString::fromLatin1(crossRel), QStringLiteral("1"));
                    // Real Wasabi FrameWnd honours the frame's own
                    // min{width,height}: the REMAINDER pane never shrinks
                    // below it and the FIXED pane is capped so both fit.
                    // Without this, a fixed pane larger than the parent
                    // (Bento playlist.dualwnd: albumart fixed 100 in a
                    // 92px strip) drives the remainder negative → the
                    // playlist collapses to 0.  Carry the min on both
                    // pseudo-panes so Widget::resolveRectFromAttrs can
                    // enforce it once the parent pixel size is known.
                    const int minRem = vertical
                        ? el.attrs.value(QStringLiteral("minwidth")).toInt()
                        : el.attrs.value(QStringLiteral("minheight")).toInt();
                    if (fixedHere) {
                        // Fixed extent, anchored to this pane's edge.
                        if (fixedOnSecond) {
                            // far edge: x = parent - size
                            pseudo.attrs.insert(QString::fromLatin1(mainPos),
                                QString::number(-defaultSize));
                            pseudo.attrs.insert(QString::fromLatin1(mainRelPos), QStringLiteral("1"));
                        } else {
                            pseudo.attrs.insert(QString::fromLatin1(mainPos), QStringLiteral("0"));
                        }
                        pseudo.attrs.insert(QString::fromLatin1(mainSz),
                            QString::number(defaultSize));
                        if (minRem > 0) {
                            // Cap to parentExtent - minRem; re-anchor if far edge.
                            pseudo.attrs.insert(
                                QStringLiteral("_frame_cap_") + QString::fromLatin1(mainSz),
                                QString::number(minRem));
                            if (fixedOnSecond)
                                pseudo.attrs.insert(QStringLiteral("_frame_cap_far"),
                                                    QStringLiteral("1"));
                        }
                    } else {
                        // Remainder pane: parentExtent - size, anchored
                        // to the opposite edge from the fixed pane.  The
                        // panes ABUT (no shortening) — shortening the
                        // remainder pane to open a sizer gap exposed its
                        // own inner-bevel highlight as a stray grey line.
                        // Instead the divider is drawn over the FIXED pane's
                        // edge (which carries only a frame, no buttons).
                        if (fixedOnSecond) {
                            pseudo.attrs.insert(QString::fromLatin1(mainPos), QStringLiteral("0"));
                        } else {
                            pseudo.attrs.insert(QString::fromLatin1(mainPos),
                                QString::number(defaultSize));
                        }
                        pseudo.attrs.insert(QString::fromLatin1(mainSz),
                            QString::number(-defaultSize));
                        pseudo.attrs.insert(QString::fromLatin1(mainRel), QStringLiteral("1"));
                        if (minRem > 0)
                            pseudo.attrs.insert(
                                QStringLiteral("_frame_min_") + QString::fromLatin1(mainSz),
                                QString::number(minRem));
                    }
                } else {
                    pseudo.attrs.insert(QStringLiteral("fitparent"),
                                        QStringLiteral("1"));
                }
                visit(pseudo, node, instanceId);
            };
            addPane(first,  /*isFirst=*/true);
            addPane(second, /*isFirst=*/false);

            // Frame divider state for the live FrameWnd model — getPosition()
            // returns `_frame_divpos` (the live pullbarpos, initialised to the
            // declared size) and setPosition() rewrites it + re-splits the
            // panes via Layout::applyFrameDividerPos.  Attribute-driven, no
            // skin names.
            {
                const int minRem = vertical
                    ? el.attrs.value(QStringLiteral("minwidth")).toInt()
                    : el.attrs.value(QStringLiteral("minheight")).toInt();
                node.attrs.insert(QStringLiteral("_frame_divpos"),
                                  QString::number(defaultSize));
                node.attrs.insert(QStringLiteral("_frame_vertical"),
                                  vertical ? QStringLiteral("1") : QStringLiteral("0"));
                node.attrs.insert(QStringLiteral("_frame_fromfar"),
                                  fixedOnSecond ? QStringLiteral("1") : QStringLiteral("0"));
                node.attrs.insert(QStringLiteral("_frame_minrem"),
                                  QString::number(minRem));
            }

            // Synthetic frame divider — a Wasabi:Frame paints a divider
            // bitmap at the split unless it opts out with v/hbitmap="empty"
            // (Bento's player.mainframe does, providing a manual grabber
            // layer instead).  Reuse the LayerWidget renderer so any skin's
            // frame dividers show without a Maki pass.  This draws the bar
            // between Bento's file-info block and the full-height playlist
            // column, and below the list.
            if (defaultSize > 0 && resizeable &&
                !qEnvironmentVariableIsSet("WASABIQT_NO_FRAMEDIV")) {
                const QString divImg = vertical
                    ? el.attrs.value(QStringLiteral("vbitmap"))
                    : el.attrs.value(QStringLiteral("hbitmap"));
                const bool optOut = divImg.compare(
                    QStringLiteral("empty"), Qt::CaseInsensitive) == 0;
                if (!optOut) {
                    // Draw the divider as a fixed-width textured bar laid
                    // over the FIXED pane's inner edge.  The panes ABUT (no
                    // sizer shortening — that exposed the remainder pane's
                    // inner-bevel highlight as a stray grey line).  The fixed
                    // pane (Bento playlist) carries only a frame at its edge,
                    // no buttons, so the bar overlaps nothing interactive;
                    // the remainder pane (file-info, with the edge-anchored
                    // buttons) stays untouched and dark right up to the bar.
                    // A grey base fill + the divider bitmap groove over it
                    // give the 3-D texture (not a flat colour).
                    const int barW = kFrameSizerWidth > 0 ? kFrameSizerWidth : 8;
                    // The fixed pane carries a ~7px frame bevel (Bento
                    // pledit.background.left) at its inner edge whose
                    // highlight+dark-body would otherwise show as a stray
                    // line just outside our bar.  Sit the bar ON that bevel
                    // so the whole boundary reads as one solid textured
                    // divider.  WASABIQT_DIVOFF overrides for tuning.
                    int bevel = qEnvironmentVariableIsSet("WASABIQT_DIVOFF")
                              ? qEnvironmentVariableIntValue("WASABIQT_DIVOFF") : 7;
                    auto setBarRect = [&](Element &e) {
                        if (vertical) {
                            if (fixedOnSecond) {
                                e.attrs.insert(QStringLiteral("x"),
                                    QString::number(-defaultSize - bevel));
                                e.attrs.insert(QStringLiteral("relatx"), QStringLiteral("1"));
                            } else {
                                e.attrs.insert(QStringLiteral("x"),
                                    QString::number(defaultSize - barW + bevel));
                            }
                            e.attrs.insert(QStringLiteral("w"), QString::number(barW));
                            e.attrs.insert(QStringLiteral("y"), QStringLiteral("0"));
                            e.attrs.insert(QStringLiteral("h"), QStringLiteral("0"));
                            e.attrs.insert(QStringLiteral("relath"), QStringLiteral("1"));
                        } else {
                            if (fixedOnSecond) {
                                e.attrs.insert(QStringLiteral("y"),
                                    QString::number(-defaultSize));
                                e.attrs.insert(QStringLiteral("relaty"), QStringLiteral("1"));
                            } else {
                                e.attrs.insert(QStringLiteral("y"),
                                    QString::number(defaultSize - barW));
                            }
                            e.attrs.insert(QStringLiteral("h"), QString::number(barW));
                            e.attrs.insert(QStringLiteral("x"), QStringLiteral("0"));
                            e.attrs.insert(QStringLiteral("w"), QStringLiteral("0"));
                            e.attrs.insert(QStringLiteral("relatw"), QStringLiteral("1"));
                        }
                    };
                    // 1) grey base fill.
                    Element baseEl;
                    baseEl.tag = QStringLiteral("layer");
                    baseEl.attrs.insert(QStringLiteral("ghost"), QStringLiteral("1"));
                    baseEl.attrs.insert(QStringLiteral("color"),
                                        QStringLiteral("58,64,67"));
                    setBarRect(baseEl);
                    visit(baseEl, node, instanceId);
                    // 2) the real divider bitmap over it (the groove/texture).
                    Element divEl;
                    divEl.tag = QStringLiteral("layer");
                    divEl.attrs.insert(QStringLiteral("ghost"), QStringLiteral("1"));
                    divEl.attrs.insert(QStringLiteral("image"),
                        !divImg.isEmpty() ? divImg
                        : (vertical
                            ? QStringLiteral("wasabi.framewnd.verticaldivider")
                            : QStringLiteral("wasabi.framewnd.horizontaldivider")));
                    setBarRect(divEl);
                    visit(divEl, node, instanceId);
                }
            }

            // Expose the divider size so scripts can read it via
            // getPosition().  Wasabi FrameWnd.getPosition() returns the
            // extent of the fixed/"poppler" pane — the `width`/`height`
            // literal we just laid the panes out from.  We computed the
            // pane geometry correctly but never published this value, so
            // `frame.getPosition()` read the absent `position` attr as 0.
            // Bento's pledit.m gates the whole playlist pane on
            // `dualwnd.getPosition() > 0` (else `wdh_pl.hide()`), so the
            // playlist never showed even though its pane was laid out.
            // General + attribute-driven, no skin names: any Wasabi:Frame
            // now reports its divider position.
            node.attrs.insert(QStringLiteral("position"),
                              QString::number(defaultSize > 0 ? defaultSize : 0));

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
        // The requested layout name is only a convention; honor the
        // container's actual default (its first <layout>) so skins that
        // name theirs differently still load.
        layout = firstLayout(*container);
    }
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
        // The drawer + shadow are *script-toggled* by the skin (the
        // CONFIG button's onLeftClick runs setVisible(1)).  Show them by
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
    if (!::getenv("WASABIQT_NO_SCRIPTHIDE"))
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

    // The layout root's own background= is the window face: the
    // rearmost fill behind everything, exactly like a group's (skins
    // such as winamp1 declare <layout background="player.main..."/>
    // and paint their whole chrome that way).  Synthesize the same
    // fill layer expandChildren produces for groups, front of list.
    {
        const QString rootBg =
            layout->attrs.value(QStringLiteral("background"));
        if (!rootBg.isEmpty()) {
            Element bgEl;
            bgEl.tag = QStringLiteral("layer");
            bgEl.attrs.insert(QStringLiteral("id"),
                              QStringLiteral("__groupbg"));
            bgEl.attrs.insert(QStringLiteral("image"), rootBg);
            bgEl.attrs.insert(QStringLiteral("x"), QStringLiteral("0"));
            bgEl.attrs.insert(QStringLiteral("y"), QStringLiteral("0"));
            bgEl.attrs.insert(QStringLiteral("w"), QStringLiteral("0"));
            bgEl.attrs.insert(QStringLiteral("h"), QStringLiteral("0"));
            bgEl.attrs.insert(QStringLiteral("relatw"), QStringLiteral("1"));
            bgEl.attrs.insert(QStringLiteral("relath"), QStringLiteral("1"));
            bgEl.attrs.insert(QStringLiteral("tile"), QStringLiteral("1"));
            out.children.insert(out.children.begin(),
                                makeResolved(bgEl));
        }
    }

    // Resolve the `:componentname` / `:containerid` skin variables to the
    // container's declared name / id — the Winamp API's public-var
    // substitution.  This is what makes a dynamic container's titlebar read
    // "PLAYLIST EDITOR" (from `<container name="Playlist Editor">`) instead of
    // the raw token; the player window dodges it only because its title is a
    // literal sendparams override.  Scoped to the text-bearing attrs so
    // image/include resolution is untouched, and run AFTER sendparams so a
    // literal title (no token) is left alone.  General: fixes every dynamic
    // container's title (Playlist Editor, Media Library, Video, …).
    {
        const QString cname = container->attrs.value(QStringLiteral("name"));
        auto subst = [&](QString v) -> QString {
            if (!cname.isEmpty()) {
                v.replace(QLatin1String(":componentname"), cname, Qt::CaseInsensitive);
                v.replace(QLatin1String("@componentname@"), cname, Qt::CaseInsensitive);
            }
            v.replace(QLatin1String(":containerid"), containerId, Qt::CaseInsensitive);
            v.replace(QLatin1String("@containerid@"), containerId, Qt::CaseInsensitive);
            return v;
        };
        std::function<void(ResolvedWidget &)> xlate = [&](ResolvedWidget &w) {
            for (const char *key : { "default", "text" }) {
                auto it = w.attrs.find(QString::fromLatin1(key));
                if (it != w.attrs.end() &&
                    (it.value().contains(QLatin1Char(':')) ||
                     it.value().contains(QLatin1Char('@')))) {
                    const QString before = it.value();
                    it.value() = subst(before);
                    if (before != it.value() &&
                        qEnvironmentVariableIntValue("WASABIQT_TRACE_MAKI") == 1)
                        fprintf(stderr, "[cvar] %s.%s: '%s' -> '%s'\n",
                                w.id.toLocal8Bit().constData(), key,
                                before.toLocal8Bit().constData(),
                                it.value().toLocal8Bit().constData());
                }
            }
            for (auto &c : w.children) if (c) xlate(*c);
        };
        xlate(out);
    }

    // Static well-known-script equivalents (titlebar resizeObjects
    // etc.) are not run here.  The post-resolve mutation step belongs
    // to whoever owns it: callers that drive Maki via SkinRuntime get
    // widget mutations from the dispatched scripts; callers that want
    // the static path can call `runKnownScripts(out, layoutW)`
    // themselves before rendering.

    return true;
}

// ──────────────────────────────────────────────────────────────────
// Static equivalents of well-known Maki scripts.  Each does the
// minimum geometry/visibility manipulation a script's onScriptLoaded
// + onResize handlers would otherwise do at runtime.

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

    // Text width = the Wasabi Text widget's SUGGESTED_W (getAutoWidth):
    // `advance + 4 + lpadding + rpadding`.  This MUST match what the Maki
    // getAutoWidth binding (wq_widget_textWidth) returns and what the
    // renderer paints, all three using the same per-font pixel mapping
    // (wasabiFontPixelSize) and the same +4 box pad.  Earlier this path
    // used a 6/7 ratio and a +11 GDI fudge to compensate for the old
    // under-sized 5/7 render; with the faithful font mapping those fudges
    // over-size the box and the right streak no longer matches the title.
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
            const bool bold = title->attrs.value(QStringLiteral("bold")) ==
                QStringLiteral("1");
            if (ok && fontsize > 0)
                f.setPixelSize(wasabiFontPixelSize(fontsize, family, bold));
            if (bold) f.setBold(true);
            QFontMetrics fm(f);
            const int lpad =
                title->attrs.value(QStringLiteral("lpadding")).toInt();
            const int rpad =
                title->attrs.value(QStringLiteral("rpadding")).toInt();
            textWidth = fm.horizontalAdvance(s) + 4 + lpad + rpad;
        }
    }
    if (textWidth <= 0) textWidth = 50;

    // Mirror titlebar.m::resizeObjects() exactly:
    //
    //   lx = (layout_width - text_width) / 2;     // layout coords
    //   lx -= sg.getLeft();                       // → titlebar-local
    //   center.setXmlParam("x", lx);              // box left = lx
    //   left.setXmlParam ("x", padleft);
    //   left.setXmlParam ("w", lx - padleft);
    //   right.setXmlParam("x", lx + text_width + 1);
    //   right.setXmlParam("w", -(lx + text_width + padright + 2));
    //   right.setXmlParam("relatw", "1");
    //
    // The box left is `lx` with NO extra inset: the renderer draws the
    // glyph at box.left + (2 - shadowx), so the 1px left bearing inside
    // the +4-wide box is what gives the title its faithful slight-left
    // bias.  (The old `cen = 2` double-counted that inset and pushed the
    // title right.)  Centring is against the layout width, not the
    // titlebar group's own width, so off-centre frames (e.g.
    // Wasabi:MainFrame:NoStatus, titlebar at x=10) stay on-axis.
    const int cen = 0;
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
            if (qEnvironmentVariableIntValue("WASABIQT_TRACE_MAKI") == 1)
                fprintf(stderr, "[titlebar] resize id=%s tag=%s lw=%d x=%d\n",
                        w.id.toLocal8Bit().constData(),
                        w.tag.toLocal8Bit().constData(), layoutWidth, titlebarX);
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
                            const bool bold = src->attrs.value(
                                QStringLiteral("bold")) == QStringLiteral("1");
                            if (ok && fs > 0)
                                f.setPixelSize(wasabiFontPixelSize(fs, family, bold));
                            if (bold) f.setBold(true);
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
enum class RegionPass { Additive, Subtractive, Ordered };

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

// True if this node has any descendant that declares a meaningful
// region directive (`sysregion` / `regionop`, add or cutout) — i.e.,
// the group backs its silhouette with explicit region contribution
// layers rather than relying solely on the blind container rect-fill.
bool hasRegionContribDescendant(const ResolvedWidget &node) {
    auto meaningful = [](const QString &v) {
        return !v.isEmpty() && v != QStringLiteral("0");
    };
    for (const auto &c : node.children) {
        if (!c) continue;
        if (meaningful(c->attrs.value(QStringLiteral("sysregion"))) ||
            meaningful(c->attrs.value(QStringLiteral("regionop"))))
            return true;
        if (hasRegionContribDescendant(*c)) return true;
    }
    return false;
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
        // In the VISUAL (Ordered) pass a container's blind rect-fill
        // re-adds pixels in z-order, so a fill composed AFTER a
        // sibling's cutout re-widens whatever the cutout carved.  That
        // is correct only when the group actually paints opaque chrome
        // there — the player's CONFIG bevel (backed by sysregion="1"
        // layers) legitimately re-adds over the drawer's edge cutout.
        // A pure overlay group with no opaque backing re-adds nothing
        // real: WinampModernPP's player.normal.drawer.shadow has no
        // `background` and only ghost shadow layers, carrying
        // sysregion="1" solely on the instance.  Its full-width fill,
        // composed after the drawer's narrowing-strip cutout, re-widens
        // the drawer's carved bottom edge — the 3px grey sliver poking
        // past the player's rounded corner.  So in the visual pass only
        // fill when the group is backed by an opaque `background`
        // texture or by its own region contribution layers.  The input
        // region (Additive/Subtractive two-pass) keeps the conservative
        // full-rect fill — over-inclusion is the safe direction there.
        bool doFill = (pass == RegionPass::Additive);
        if (pass == RegionPass::Ordered) {
            const bool hasBackground = !w.attrs
                .value(QStringLiteral("background")).isEmpty();
            doFill = hasBackground || hasRegionContribDescendant(w);
        }
        if (sr == QStringLiteral("1") && doFill) {
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
                pass == RegionPass::Ordered ||
                (cutoutMode ? (pass == RegionPass::Subtractive)
                            : (pass == RegionPass::Additive));
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

QRegion computeVisualRegion(const ResolvedWidget &root,
                            BitmapRegistry &registry,
                            QSize canvas) {
    if (canvas.width() <= 0 || canvas.height() <= 0)
        return QRegion();

    QImage buf(canvas, QImage::Format_ARGB32_Premultiplied);
    buf.fill(Qt::transparent);
    bool foundAny = false;
    {
        QPainter p(&buf);
        p.setRenderHint(QPainter::Antialiasing,          false);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        // Single ORDERED walk — Win32 SetWindowRgn composition: each
        // contribution applies in tree (z) order, so a sysregion="-N"
        // cutout only carves what was composed below it and chrome
        // painted above re-adds its pixels.  Modern's stack needs the
        // order both ways: the drawer's edge masks carve its strips
        // early, then player.main / window.bg2 re-add the player band
        // (the hatched CONFIG bevel that overlaps the drawer's top
        // rows), and the player's own corner masks carve its rounded
        // bottom corners last.  The two-pass computeWindowRegion —
        // subtract-always-wins — would eat that bevel: right for a
        // conservative input region, wrong for the visible silhouette.
        paintRegionLayers(p, root, registry, canvas, foundAny,
                          RegionPass::Ordered);
    }
    if (!foundAny) return QRegion();
    return regionFromAlpha(buf);
}

namespace {

// Bbox of a layer in its parent's coord system, expressed in a way
// that's comparable across siblings without knowing the parent's
// canvas dims.  Returns (x, y, w, h) in raw-attr space; the caller
// only compares bboxes that share the same relat-flags pair.
struct LayerBox {
    int x, y, w, h;
    bool relatx, relaty;
};
LayerBox layerBoxFor(const Widget &w, BitmapRegistry &reg) {
    LayerBox b{};
    b.x = w.attrs.value(QStringLiteral("x")).toInt();
    b.y = w.attrs.value(QStringLiteral("y")).toInt();
    b.w = w.attrs.value(QStringLiteral("w")).toInt();
    b.h = w.attrs.value(QStringLiteral("h")).toInt();
    b.relatx = (w.attrs.value(QStringLiteral("relatx")) ==
                QStringLiteral("1"));
    b.relaty = (w.attrs.value(QStringLiteral("relaty")) ==
                QStringLiteral("1"));
    // Fall back to bitmap's natural dimensions when w/h aren't set
    // (Wasabi convention — same fallback paintLayer uses).
    if (b.w <= 0 || b.h <= 0) {
        const QString img = w.attrs.value(QStringLiteral("image"));
        if (!img.isEmpty()) {
            const QImage src = reg.imageFor(img);
            if (!src.isNull()) {
                if (b.w <= 0) b.w = src.width();
                if (b.h <= 0) b.h = src.height();
            }
        }
    }
    return b;
}

// True if two boxes overlap in their parent's coord system.  Only
// safe to compare when both share the same relat-flag values
// (otherwise one is "from-left" and the other "from-right" and we
// can't compare without knowing parent.w/h).
bool boxesOverlap(const LayerBox &a, const LayerBox &b) {
    if (a.relatx != b.relatx) return false;
    if (a.relaty != b.relaty) return false;
    if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0) return false;
    const int aR = a.x + a.w, aB = a.y + a.h;
    const int bR = b.x + b.w, bB = b.y + b.h;
    return !(a.x >= bR || b.x >= aR || a.y >= bB || b.y >= aB);
}

void collectChromeCutoutsRec(
        const ResolvedWidget &node,
        BitmapRegistry &reg,
        QHash<QString, QList<ChromeCutout>> &out) {
    // For each `sysregion="-N"` cutout layer in this node's
    // children, register it as a cutout against every sibling chrome
    // layer whose bbox overlaps the cutout's bbox.  The image-name
    // pairing convention (`<chrome>.region` ↔ `<chrome>`) was a
    // sufficient heuristic when each cutout had exactly one chrome
    // sibling covering its area, but skins commonly stack a second
    // opaque overlay over a corner cutout (WinampModernPP's
    // `window.bg2.right` over `window.right.bottom.region`, hosting
    // CONFIG / about UI).  Pairing only by image-name would miss the
    // overlay and leave its opaque corner pixels visible past the
    // intended round-off.  Geometric overlap catches every chrome
    // that should be cut, regardless of image-name convention.
    for (const auto &cutoutPtr : node.children) {
        if (!cutoutPtr) continue;
        const Widget &cutout = *cutoutPtr;
        if (cutout.tag != QStringLiteral("layer")) continue;
        const QString sr = cutout.attrs.value(QStringLiteral("sysregion"));
        if (sr.isEmpty() || !sr.startsWith(QChar('-'))) continue;
        const QString cutoutImg = cutout.attrs.value(QStringLiteral("image"));
        if (cutoutImg.isEmpty()) continue;
        const LayerBox cb = layerBoxFor(cutout, reg);
        for (const auto &chromePtr : node.children) {
            if (!chromePtr) continue;
            const Widget &chrome = *chromePtr;
            if (chrome.tag != QStringLiteral("layer")) continue;
            // Skip the cutout itself and any other cutout layer.
            if (&chrome == &cutout) continue;
            const QString chromeSr =
                chrome.attrs.value(QStringLiteral("sysregion"));
            if (!chromeSr.isEmpty() &&
                chromeSr.startsWith(QChar('-')))
                continue;
            const QString chromeImg =
                chrome.attrs.value(QStringLiteral("image"));
            if (chromeImg.isEmpty()) continue;
            const LayerBox ch = layerBoxFor(chrome, reg);
            if (!boxesOverlap(cb, ch)) continue;
            ChromeCutout cc;
            cc.cutoutImage = cutoutImg;
            cc.offset      = QPoint(cb.x - ch.x, cb.y - ch.y);
            out[chromeImg].append(cc);
        }
    }
    for (const auto &child : node.children)
        if (child) collectChromeCutoutsRec(*child, reg, out);
}

}  // namespace

QHash<QString, QList<ChromeCutout>>
collectChromeCutouts(const ResolvedWidget &root, BitmapRegistry &reg) {
    QHash<QString, QList<ChromeCutout>> out;
    collectChromeCutoutsRec(root, reg, out);
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

namespace {
// Walk one container's immediate children, find the first widget
// that declares both `cfgattrib=` and `high=` (the canonical
// "stepper anchor": a slider whose `high` defines the int range).
// Returns nullptr when this container doesn't host a stepper.
const ResolvedWidget *findStepperAnchor(
        const ResolvedWidget &parent) {
    for (const auto &child : parent.children) {
        if (!child) continue;
        const QString cfg = child->attrs.value(QStringLiteral("cfgattrib"));
        if (cfg.isEmpty()) continue;
        if (!child->attrs.contains(QStringLiteral("high"))) continue;
        return child.get();
    }
    return nullptr;
}

// Recursive walker: for each container, look for the stepper anchor,
// then wire its sibling Decrease/Increase buttons + Display text(s)
// to the same cfgattrib key.
void wireSteppersIn(ResolvedWidget &parent) {
    const ResolvedWidget *anchor = findStepperAnchor(parent);
    if (anchor) {
        const QString key = anchor->attrs.value(
            QStringLiteral("cfgattrib"));
        bool ok = false;
        int low  = anchor->attrs.value(QStringLiteral("low"),
                                        QStringLiteral("0")).toInt(&ok);
        if (!ok) low = 0;
        int high = anchor->attrs.value(QStringLiteral("high")).toInt(&ok);
        if (!ok) high = 0;
        int step = anchor->attrs.value(QStringLiteral("step"),
                                        QStringLiteral("1")).toInt(&ok);
        if (!ok || step <= 0) step = 1;
        // Tag each sibling button / display by writing the stepper
        // metadata into its attrs.  The widget subclasses
        // (ButtonWidget::onAttrsInitialized / TextWidget) read these
        // attrs to wire themselves to CfgAttribStore.  Attrs are the
        // only persistent state we can rely on — widgets are
        // re-instantiated each load via the factory.
        for (auto &child : parent.children) {
            if (!child) continue;
            const QString id = child->id;
            if (id.isEmpty()) continue;
            if (id.endsWith(QLatin1String("Decrease"),
                             Qt::CaseInsensitive) ||
                id.endsWith(QLatin1String("Increase"),
                             Qt::CaseInsensitive) ||
                id.endsWith(QLatin1String("Display"),
                             Qt::CaseInsensitive)) {
                child->attrs.insert(QStringLiteral("_stepper_key"),  key);
                child->attrs.insert(QStringLiteral("_stepper_low"),
                                     QString::number(low));
                child->attrs.insert(QStringLiteral("_stepper_high"),
                                     QString::number(high));
                child->attrs.insert(QStringLiteral("_stepper_step"),
                                     QString::number(step));
                // Re-fire the widget's init hook so its cached
                // stepper fields pick up the new attrs.  Safe to
                // call again — the cfgattrib subscription path is
                // idempotent.
                child->onAttrsInitialized();
            }
        }
    }
    // Recurse into children regardless — every nested container
    // may host its own stepper group.
    for (auto &child : parent.children) {
        if (child) wireSteppersIn(*child);
    }
}
}  // namespace

void wireSteppers(ResolvedWidget &root) {
    wireSteppersIn(root);
}

// Re-split a Wasabi:Frame against a new divider position — the runtime
// counterpart of the addPane expansion, driving the live FrameWnd model.
// `frameNode` must carry the `_frame_*` metadata planted at expansion;
// each pane child carries `_frame_pane` = fixed|rem.  Rewrites only the
// main-axis attrs (the cross axis always fills the parent and is set once
// at expansion), then re-inits each pane.  The caller re-resolves the tree
// + repaints.  Follows the Winamp FrameWnd getDividerPos/setDividerPos
// semantics: pos is the fixed pane's pixel extent (the pullbarpos).
void applyFrameDividerPos(ResolvedWidget &frameNode, int pos) {
    if (!frameNode.attrs.contains(QStringLiteral("_frame_divpos"))) return;
    if (pos < 0) pos = 0;
    frameNode.attrs.insert(QStringLiteral("_frame_divpos"), QString::number(pos));
    const bool vertical = frameNode.attrs.value(QStringLiteral("_frame_vertical")) == QStringLiteral("1");
    const bool fromFar  = frameNode.attrs.value(QStringLiteral("_frame_fromfar"))  == QStringLiteral("1");
    const int  minRem   = frameNode.attrs.value(QStringLiteral("_frame_minrem")).toInt();
    const QString mainPos    = vertical ? QStringLiteral("x") : QStringLiteral("y");
    const QString mainSz     = vertical ? QStringLiteral("w") : QStringLiteral("h");
    const QString mainRel    = vertical ? QStringLiteral("relatw") : QStringLiteral("relath");
    const QString mainRelPos = vertical ? QStringLiteral("relatx") : QStringLiteral("relaty");
    for (auto &cptr : frameNode.children) {
        if (!cptr) continue;
        ResolvedWidget &pane = *cptr;
        const QString role = pane.attrs.value(QStringLiteral("_frame_pane"));
        if (role.isEmpty()) continue;
        const bool fixedHere = (role == QStringLiteral("fixed"));
        // Clear stale main-axis flags before re-applying (a previous pos
        // may have set relat/cap/min that the new pos doesn't want).
        pane.attrs.remove(mainRel);
        pane.attrs.remove(mainRelPos);
        pane.attrs.remove(QStringLiteral("_frame_cap_") + mainSz);
        pane.attrs.remove(QStringLiteral("_frame_cap_far"));
        pane.attrs.remove(QStringLiteral("_frame_min_") + mainSz);
        if (fixedHere) {
            if (fromFar) {
                pane.attrs.insert(mainPos, QString::number(-pos));
                pane.attrs.insert(mainRelPos, QStringLiteral("1"));
            } else {
                pane.attrs.insert(mainPos, QStringLiteral("0"));
            }
            pane.attrs.insert(mainSz, QString::number(pos));
            if (minRem > 0) {
                pane.attrs.insert(QStringLiteral("_frame_cap_") + mainSz, QString::number(minRem));
                if (fromFar) pane.attrs.insert(QStringLiteral("_frame_cap_far"), QStringLiteral("1"));
            }
        } else {
            pane.attrs.insert(mainPos, fromFar ? QStringLiteral("0") : QString::number(pos));
            pane.attrs.insert(mainSz, QString::number(-pos));
            pane.attrs.insert(mainRel, QStringLiteral("1"));
            if (minRem > 0)
                pane.attrs.insert(QStringLiteral("_frame_min_") + mainSz, QString::number(minRem));
        }
        pane.onAttrsInitialized();
    }
}


// `wireMenuBackgrounds` — engine-level fix for the Bento/Big Bento
// "discontinuous titlebar" symptom.  Bento's `mainmenu.maki` uses
// OPCODE_CALLM2 (DLF-index dispatch) to look up sibling background
// layers `menu.layer.<X>.normal/hover/down` by name and set their
// width to match each menu item's text autoWidth.  The CALLM2
// dispatch path returns 0 instead of the registered binding ptr
// (DLF-table indexing issue in our Maki VM port), so the lookups
// fail silently → background layers stay at default w=0 → titlebar
// background looks discontinuous behind the menu items because
// only the text glyphs paint, no per-item highlight overlay.
//
// Workaround: after Maki dispatch, walk the tree; for each pair of
// `menu.text.X` + `menu.layer.X.{normal,hover,down}`, copy x/w from
// the text widget onto each layer sibling.  Skin-agnostic — any
// skin using the canonical Wasabi `menu.text.X` / `menu.layer.X`
// naming convention gets the per-item background highlight band
// without depending on the Maki VM's CALLM2 path.
namespace {
void collectByIdSubstring(ResolvedWidget &node,
                           const QString &substr,
                           QList<ResolvedWidget *> &out) {
    if (node.id.contains(substr, Qt::CaseInsensitive))
        out.append(&node);
    for (auto &child : node.children) {
        if (child) collectByIdSubstring(*child, substr, out);
    }
}
}  // namespace

void wireMenuAlign(ResolvedWidget &root, const SkinXml::Document &doc,
                   BitmapRegistry &reg) {
    auto bitmapWidth = [&](const QString &imgId) {
        QImage im = reg.imageFor(imgId);
        return im.isNull() ? 0 : im.width();
    };
    std::function<ResolvedWidget *(ResolvedWidget &, const QString &)> findById =
        [&](ResolvedWidget &n, const QString &id) -> ResolvedWidget * {
        if (n.id == id) return &n;
        for (auto &c : n.children)
            if (c) if (auto *r = findById(*c, id)) return r;
        return nullptr;
    };
    for (const auto &ref : doc.scripts) {
        if (!ref.file.contains(QStringLiteral("menualign"),
                                Qt::CaseInsensitive))
            continue;
        ResolvedWidget *group = findById(root, ref.ownerGroupId);
        if (!group) continue;
        int offset = 0;
        for (const QString &id : ref.param.split(QChar(','),
                                                 Qt::SkipEmptyParts)) {
            const QString name = id.trimmed();
            ResolvedWidget *target = nullptr;
            for (auto &c : group->children)
                if (c && c->id == name) { target = c.get(); break; }
            if (!target) continue;
            target->setXmlParam(QStringLiteral("x"), QString::number(offset));
            const QString aws =
                target->attrs.value(QStringLiteral("autowidthsource"));
            int w = 0;
            if (!aws.isEmpty())
                if (ResolvedWidget *src = findById(*target, aws)) {
                    const QString img =
                        src->attrs.value(QStringLiteral("image"));
                    if (!img.isEmpty()) w = bitmapWidth(img);
                }
            if (w > 0) {
                target->setXmlParam(QStringLiteral("w"), QString::number(w));
                offset += w;
            }
        }
    }
}

void resolveBitmapAutoWidths(ResolvedWidget &root, BitmapRegistry &reg) {
    // Find a descendant of `n` (excluding n) whose id matches, scoped so
    // groups that reuse a child id ("label.txt") resolve their OWN child.
    std::function<ResolvedWidget *(ResolvedWidget &, const QString &)> findIn =
        [&](ResolvedWidget &n, const QString &id) -> ResolvedWidget * {
        for (auto &c : n.children) {
            if (!c) continue;
            if (c->id == id) return c.get();
            if (ResolvedWidget *r = findIn(*c, id)) return r;
        }
        return nullptr;
    };
    std::function<void(ResolvedWidget &)> walk = [&](ResolvedWidget &w) {
        const QString aws = w.attrs.value(QStringLiteral("autowidthsource"));
        if (!aws.isEmpty()) {
            const QString curW = w.attrs.value(QStringLiteral("w"));
            if (curW.isEmpty() || curW.toInt() <= 0) {
                if (ResolvedWidget *src = findIn(w, aws)) {
                    const QString img =
                        src->attrs.value(QStringLiteral("image"));
                    if (!img.isEmpty()) {
                        const QImage im = reg.imageFor(img);
                        if (!im.isNull())
                            w.attrs.insert(QStringLiteral("w"),
                                           QString::number(im.width()));
                    }
                }
            }
        }
        for (auto &c : w.children) if (c) walk(*c);
    };
    walk(root);
}

void wireMenuBackgrounds(ResolvedWidget &root) {
    // Find all menu.text.X widgets — they're the canonical anchors
    // that Maki positions correctly.
    QList<ResolvedWidget *> textLayers;
    collectByIdSubstring(root, QStringLiteral("menu.text."), textLayers);

    QList<ResolvedWidget *> bgLayers;
    collectByIdSubstring(root, QStringLiteral("menu.layer."), bgLayers);

    if (textLayers.isEmpty() || bgLayers.isEmpty()) return;

    for (auto *tl : textLayers) {
        // menu.text.<X> → X
        const int dotPos = tl->id.lastIndexOf(QChar('.'));
        if (dotPos < 0) continue;
        const QString suffix = tl->id.mid(dotPos + 1);
        // The text widget's x/w (set by Maki) become the
        // corresponding menu.layer.X.normal/hover/down's x/w.
        const QString xVal = tl->attrs.value(QStringLiteral("x"));
        // For width, prefer the text widget's w; fall back to a
        // sensible width derived from the bound bitmap so the
        // highlight band still covers the text glyphs.
        QString wVal = tl->attrs.value(QStringLiteral("w"));
        if (wVal.isEmpty() || wVal == QStringLiteral("0")) {
            // Best-effort: read the source bitmap width via the
            // layer's `image=` attr (the bitmap registry would
            // normally resolve this).  Fall back to 30 so the
            // highlight band exists.
            wVal = QStringLiteral("30");
        }
        for (auto *bl : bgLayers) {
            // Match suffix: menu.layer.<suffix>.{normal,hover,down}
            const QString needle = QStringLiteral("menu.layer.") +
                                    suffix + QStringLiteral(".");
            if (!bl->id.startsWith(needle, Qt::CaseInsensitive)) continue;
            bl->attrs.insert(QStringLiteral("x"), xVal);
            bl->attrs.insert(QStringLiteral("w"), wVal);
        }
    }
}

// ── dumpResolved ──────────────────────────────────────────────────
namespace {

void dumpResolvedRec(const ResolvedWidget &node, QSize canvas,
                      int depth, FILE *out) {
    QRect r = Widget::resolveRectFromAttrs(node.attrs, canvas);
    const QString id = node.id;
    const QString tag = node.tag;
    const QString vis = node.attrs.value(QStringLiteral("visible"));
    const QString visTag = vis == QStringLiteral("0") ? "(hidden)" : "";
    QString indent(depth * 2, QChar(' '));
    fprintf(out, "%s%s[%s] %dx%d@(%d,%d) %s\n",
            indent.toLocal8Bit().constData(),
            tag.toLocal8Bit().constData(),
            id.isEmpty() ? "-" : id.toLocal8Bit().constData(),
            r.width(), r.height(), r.x(), r.y(),
            visTag.toLocal8Bit().constData());
    // Recurse with the resolved rect as the child canvas (Container
    // semantics — children are positioned relative to parent rect).
    const QSize childCanvas(qMax(0, r.width()), qMax(0, r.height()));
    for (const auto &child : node.children) {
        if (child) dumpResolvedRec(*child, childCanvas, depth + 1, out);
    }
}

}  // namespace

void dumpResolved(const ResolvedWidget &root, QSize canvas, FILE *out) {
    if (!out) out = stderr;
    fprintf(out, "── qtWasabi resolved tree dump (canvas=%dx%d) ──\n",
            canvas.width(), canvas.height());
    dumpResolvedRec(root, canvas, 0, out);
    fprintf(out, "── end ──\n");
    fflush(out);
}

}  // namespace qtWasabi::Layout
