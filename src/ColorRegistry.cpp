// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/ColorRegistry.h>
#include <WasabiQt/GammasetRegistry.h>
#include <WasabiQt/SkinXml.h>

#include <QImage>

namespace WasabiQt {

namespace {
void collect(const SkinXml::Element &el,
             QHash<QString, ColorRegistry::Entry> &out) {
    if (el.tag == QStringLiteral("color")) {
        const QString id = el.attrs.value(QStringLiteral("id"));
        const QString value = el.attrs.value(QStringLiteral("value"));
        if (!id.isEmpty() && value.contains(QChar(','))) {
            const auto parts = value.split(QChar(','));
            if (parts.size() == 3) {
                ColorRegistry::Entry e;
                e.rgb = QColor(parts[0].trimmed().toInt(),
                                parts[1].trimmed().toInt(),
                                parts[2].trimmed().toInt());
                e.gammagroup = el.attrs.value(
                    QStringLiteral("gammagroup"));
                out.insert(id, e);
            }
        }
    }
    for (const auto &c : el.children) collect(c, out);
}

// The same per-channel transform GammasetRegistry::applyToImage
// uses — we don't have an alpha to feed through, just multiply
// each channel by `rm/65535` where rm = 65535 + (t.X << 4).
QColor applyGamma(QColor base, const GammaGroup &t) {
    if (t.r == 0 && t.g == 0 && t.b == 0 && t.gray == 0 && t.boost == 0)
        return base;
    int R = base.red();
    int G = base.green();
    int B = base.blue();
    if (t.gray == 1) {
        const int m = qMax(R, qMax(G, B));
        R = G = B = m;
    } else if (t.gray == 2) {
        const int m = (R + G + B) / 3;
        R = G = B = m;
    }
    if (t.boost) {
        // For a colour token we don't have the alpha multiplier,
        // approximate by shifting toward white by 50%.
        R = qMin(255, (R >> 1) + 127);
        G = qMin(255, (G >> 1) + 127);
        B = qMin(255, (B >> 1) + 127);
    }
    const int rm = 65535 + (t.r << 4);
    const int gm = 65535 + (t.g << 4);
    const int bm = 65535 + (t.b << 4);
    R = qBound(0, (R * rm) >> 16, 255);
    G = qBound(0, (G * gm) >> 16, 255);
    B = qBound(0, (B * bm) >> 16, 255);
    return QColor(R, G, B);
}
}  // namespace

int ColorRegistry::loadFromDocument(const SkinXml::Document &doc) {
    m_colors.clear();
    collect(doc.root, m_colors);
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
        base = applyGamma(base, t);
    }
    return base;
}

}  // namespace WasabiQt
