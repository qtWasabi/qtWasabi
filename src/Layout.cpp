// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/Layout.h>
#include <WasabiQt/SkinXml.h>

#include <QHash>
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
void collectHideObjects(const Element &el, QSet<QString> &out) {
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
        root.attrs.value(QStringLiteral("id")) == id)
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
               tag == QStringLiteral("colorthemes_list") ||
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
    SendparamsMap sendparams;
    collectSendparams(doc.root, sendparams);
    QSet<QString> hidden;
    collectHideObjects(doc.root, hidden);
    // Wasabi's standardframe.maki / videoavs.maki / pledit.maki etc.
    // call setVisible(0) on these container groups during onLoad.
    // Until our Maki bindings actually run those scripts, treat the
    // groups as default-hidden so static rendering produces a sane
    // approximation of the in-app render.
    static const QStringList kScriptHiddenByDefault {
        QStringLiteral("player.normal.drawer"),
        QStringLiteral("player.normal.drawer.shadow"),
        QStringLiteral("AVSGroup"),
        QStringLiteral("player.normal.video"),
        QStringLiteral("player.shade.drawer"),
    };
    for (const auto &id : kScriptHiddenByDefault) hidden.insert(id);

    out = makeResolved(*layout);
    Expander ex(groupdefs, sendparams, hidden);
    ex.expandChildren(*layout, out, /*instanceId*/ {});

    // Static equivalents of well-known Maki scripts run here.  Each
    // mirrors the geometry/visibility manipulation a script's load-
    // time handlers would otherwise do.  Removed when M13 ships
    // real Maki bindings.
    extern void runKnownScripts(ResolvedWidget &, int);
    int layoutW = out.attrs.value(QStringLiteral("w")).toInt();
    if (layoutW <= 0) layoutW = out.attrs.value(QStringLiteral("minimum_w")).toInt();
    if (layoutW <= 0) layoutW = 354;
    runKnownScripts(out, layoutW);

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

    // Approximate text width.  Real titlebar.m calls
    // center.getAutoWidth() which runs QFontMetrics::horizontalAdvance.
    // We don't have a QPainter here, so estimate from char count.
    // ~6px per char at fontsize=14 (after 4/7 ratio).
    int textWidth = 0;
    if (title) {
        const QString defaultText = title->attrs.value(
            QStringLiteral("default"));
        const int chars = qMax(1, defaultText.size());
        const int fontsize = title->attrs.value(
            QStringLiteral("fontsize")).toInt();
        const int qpx = qMax(8, (fontsize * 4 + 3) / 7);
        // Heuristic: ~0.55 * pixel size per char for Arial bold.
        textWidth = static_cast<int>(chars * qpx * 0.55) + 4;
    }
    if (textWidth <= 0) textWidth = 50;

    // resizeObjects: lx = (layout_width - text_width) / 2.  The
    // titlebar groupdef instance lives inside a frame at x=4 y=0,
    // and the titlebar is itself 22px shy of the layout edge — but
    // for the streak math, use the titlebar group's local coord.
    const int titlebarW = titlebar.attrs.value(
        QStringLiteral("w")).toInt();
    const int titlebarRelW = titlebar.attrs.value(
        QStringLiteral("relatw")).toInt();
    int innerW = (titlebarRelW != 0) ? layoutWidth + titlebarW : titlebarW;
    if (innerW <= 0) innerW = layoutWidth - 22;

    const int lx = (innerW - textWidth) / 2;

    if (title) {
        title->attrs.insert(QStringLiteral("x"), QString::number(lx));
        title->attrs.insert(QStringLiteral("w"), QString::number(textWidth));
        title->attrs.remove(QStringLiteral("relatx"));
        title->attrs.remove(QStringLiteral("relatw"));
    }
    if (titleOverlay) {
        titleOverlay->attrs.insert(QStringLiteral("x"),
                                    QString::number(lx));
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
        // streak.right.w = -(lx + textWidth + padright + 2) relatw=1
        // i.e. relative to titlebar width: -((rightX) + padright + 1)
        const int rightWNeg = -(rightX + padRight + 1);
        streakRight->attrs.insert(QStringLiteral("w"),
                                   QString::number(rightWNeg));
        streakRight->attrs.insert(QStringLiteral("relatw"),
                                   QStringLiteral("1"));
        streakRight->attrs.remove(QStringLiteral("relatx"));
    }
}

// Walk the resolved tree, find <Wasabi:TitleBar> instances, run the
// titlebar resize equivalent against each.
void applyTo(ResolvedWidget &root, int layoutWidth) {
    std::function<void(ResolvedWidget &, int, int)> walk =
        [&](ResolvedWidget &w, int padLeft, int padRight) {
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
            applyTitlebarResize(w, layoutWidth, newPadLeft, newPadRight);
        }

        for (auto &c : w.children)
            walk(c, newPadLeft, newPadRight);
    };
    walk(root, 0, 0);
}

}  // namespace knownscripts

void runKnownScripts(ResolvedWidget &root, int layoutWidth) {
    knownscripts::applyTo(root, layoutWidth);
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

}  // namespace WasabiQt::Layout
