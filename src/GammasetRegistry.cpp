// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/GammasetRegistry.h>
#include <WasabiQt/SkinXml.h>

#include <QImage>
#include <QString>
#include <algorithm>

namespace WasabiQt {

namespace {

void parseValue(const QString &csv, GammaGroup &out) {
    const auto parts = csv.split(QChar(','));
    if (parts.size() >= 1) out.r = parts[0].toInt();
    if (parts.size() >= 2) out.g = parts[1].toInt();
    if (parts.size() >= 3) out.b = parts[2].toInt();
}

void collect(const SkinXml::Element &el, QHash<QString, Gammaset> &sets) {
    if (el.tag == QStringLiteral("gammaset")) {
        Gammaset s;
        s.name = el.attrs.value(QStringLiteral("id"));
        for (const auto &child : el.children) {
            if (child.tag != QStringLiteral("gammagroup")) continue;
            const QString id = child.attrs.value(QStringLiteral("id"));
            if (id.isEmpty()) continue;
            GammaGroup g;
            parseValue(child.attrs.value(QStringLiteral("value")), g);
            g.gray  = child.attrs.value(QStringLiteral("gray")).toInt();
            g.boost = child.attrs.value(QStringLiteral("boost")).toInt();
            s.groups.insert(id, g);
        }
        if (!s.name.isEmpty()) sets.insert(s.name, std::move(s));
    }
    for (const auto &child : el.children) collect(child, sets);
}

}  // namespace

int GammasetRegistry::loadFromDocument(const SkinXml::Document &doc) {
    m_sets.clear();
    m_active = nullptr;
    collect(doc.root, m_sets);
    if (auto it = m_sets.constFind(QStringLiteral("Default"));
        it != m_sets.constEnd())
        m_active = &it.value();
    return m_sets.size();
}

const Gammaset *GammasetRegistry::find(const QString &name) const {
    auto it = m_sets.constFind(name);
    return it == m_sets.constEnd() ? nullptr : &it.value();
}

void GammasetRegistry::setActiveGammaset(const QString &name) {
    auto it = m_sets.constFind(name);
    m_active = (it == m_sets.constEnd()) ? nullptr : &it.value();
}

GammaGroup GammasetRegistry::transformFor(const QString &gammagroup) const {
    if (!m_active || gammagroup.isEmpty()) return {};
    auto it = m_active->groups.constFind(gammagroup);
    return it == m_active->groups.constEnd() ? GammaGroup{} : it.value();
}

void GammasetRegistry::applyToImage(QImage &img, const GammaGroup &t) {
    if (img.isNull()) return;
    const bool identity = t.r == 0 && t.g == 0 && t.b == 0 &&
                          t.gray == 0 && t.boost == 0;
    if (identity) return;

    if (img.format() != QImage::Format_ARGB32 &&
        img.format() != QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_ARGB32);

    // Pre-compute multipliers for the linear-RGB transform.  Mirrors
    // upstream Winamp's GammaFilter::filterBitmap.
    const int rm = 65535 + (t.r << 4);
    const int gm = 65535 + (t.g << 4);
    const int bm = 65535 + (t.b << 4);

    const int w = img.width();
    const int h = img.height();
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb p = line[x];
            int A = qAlpha(p);
            int R = qRed(p);
            int G = qGreen(p);
            int B = qBlue(p);

            // gray=1: take MAX of channels (luminance-by-max).
            // gray=2: take average.
            if (t.gray == 1) {
                const int m = std::max({R, G, B});
                R = G = B = m;
            } else if (t.gray == 2) {
                const int m = (R + G + B) / 3;
                R = G = B = m;
            }

            // boost: shift each channel toward white by alpha/2.
            if (t.boost) {
                const int add = A >> 1;
                R = std::min(255, (R >> 1) + add);
                G = std::min(255, (G >> 1) + add);
                B = std::min(255, (B >> 1) + add);
            }

            // Linear per-channel transform — channel_out = R * rm / 65535.
            R = (R * rm) >> 16;
            G = (G * gm) >> 16;
            B = (B * bm) >> 16;
            R = std::clamp(R, 0, 255);
            G = std::clamp(G, 0, 255);
            B = std::clamp(B, 0, 255);

            line[x] = qRgba(R, G, B, A);
        }
    }
}

}  // namespace WasabiQt
