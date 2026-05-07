// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/Layout.h>
#include <WasabiQt/SkinXml.h>

#include <QHash>
#include <QSet>
#include <QString>

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
             const SendparamsMap &sendparams)
        : m_groupdefs(groupdefs), m_sendparams(sendparams) {}

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
            applySendparams(node, instanceId);
            expandChildren(*def, node,
                           iid.isEmpty() ? instanceId : iid);

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

    const GroupdefIndex &m_groupdefs;
    const SendparamsMap &m_sendparams;
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

    out = makeResolved(*layout);
    Expander ex(groupdefs, sendparams);
    ex.expandChildren(*layout, out, /*instanceId*/ {});
    return true;
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
