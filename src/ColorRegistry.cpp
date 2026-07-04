// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <qtWasabi/ColorRegistry.h>
#include <qtWasabi/GammasetRegistry.h>
#include <qtWasabi/SkinXml.h>

#include <QImage>

#include <functional>
#include <utility>

namespace qtWasabi {

namespace {
// Collect every <color id value gammagroup> as raw strings.  `value` may
// be a literal "r,g,b" OR a reference to another color id — Modern skins
// indirect (e.g. wasabi.popupmenu.background -> color.display.bg), which
// the literal-only loader used to drop.
void collectRaw(const SkinXml::Element &el,
                QHash<QString, std::pair<QString, QString>> &out) {
    if (el.tag == QStringLiteral("color")) {
        const QString id = el.attrs.value(QStringLiteral("id"));
        const QString value = el.attrs.value(QStringLiteral("value"));
        if (!id.isEmpty() && !value.isEmpty())
            out.insert(id, { value,
                el.attrs.value(QStringLiteral("gammagroup")) });
    }
    for (const auto &c : el.children) collectRaw(c, out);
}

}  // namespace

int ColorRegistry::loadFromDocument(const SkinXml::Document &doc) {
    m_colors.clear();
    QHash<QString, std::pair<QString, QString>> raw;
    collectRaw(doc.root, raw);
    // Resolve each value, following id references to the literal at the
    // end of the chain (cycle-guarded).  A referencing entry inherits the
    // chain's gammagroup unless it declares its own.
    std::function<Entry(const QString &, const QString &, int)> resolveRaw =
        [&](const QString &value, const QString &ownGamma, int depth) -> Entry {
        Entry e;
        e.gammagroup = ownGamma;
        if (depth > 8) return e;                 // cycle / runaway guard
        if (value.contains(QChar(','))) {
            const auto parts = value.split(QChar(','));
            if (parts.size() == 3)
                e.rgb = QColor(parts[0].trimmed().toInt(),
                                parts[1].trimmed().toInt(),
                                parts[2].trimmed().toInt());
            return e;
        }
        auto it = raw.constFind(value);
        if (it == raw.constEnd()) return e;      // unresolved → invalid rgb
        const Entry sub = resolveRaw(it->first, it->second, depth + 1);
        e.rgb = sub.rgb;
        if (e.gammagroup.isEmpty()) e.gammagroup = sub.gammagroup;
        return e;
    };
    for (auto it = raw.constBegin(); it != raw.constEnd(); ++it) {
        const Entry e = resolveRaw(it.value().first, it.value().second, 0);
        if (e.rgb.isValid()) m_colors.insert(it.key(), e);
    }
    return m_colors.size();
}

QColor ColorRegistry::resolve(const QString &value,
                              const GammasetRegistry *gammasets,
                              QColor fallback) const {
    if (value.isEmpty()) return fallback;
    // Literal "r,g,b" form: parse directly, no gammaset.
    if (value.contains(QChar(','))) {
        const auto parts = value.split(QChar(','));
        if (parts.size() == 3) {
            return QColor(parts[0].trimmed().toInt(),
                           parts[1].trimmed().toInt(),
                           parts[2].trimmed().toInt());
        }
        return fallback;
    }
    auto it = m_colors.constFind(value);
    if (it == m_colors.constEnd()) return fallback;
    QColor base = it->rgb;
    if (gammasets && !it->gammagroup.isEmpty()) {
        const GammaGroup t = gammasets->transformFor(it->gammagroup);
        base = GammasetRegistry::applyToColor(base, t);
    }
    // A global recolor theme tints every colour too, so chrome derived from
    // the skin palette follows the recolour (applyToColor no-ops on identity).
    if (gammasets)
        base = GammasetRegistry::applyToColor(base,
                                              gammasets->globalTransform());
    return base;
}

}  // namespace qtWasabi
