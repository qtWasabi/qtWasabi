// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/GammasetRegistry.h>
#include <WasabiQt/SkinXml.h>

#include <QDir>
#include <QImageReader>
#include <QPainter>

namespace WasabiQt {

namespace {
void collectBitmaps(const SkinXml::Element &el,
                    QHash<QString, BitmapDef> &out)
{
    if (el.tag == QStringLiteral("bitmap")) {
        BitmapDef d;
        d.id   = el.attrs.value(QStringLiteral("id"));
        d.file = el.attrs.value(QStringLiteral("file"));
        d.gammagroup = el.attrs.value(QStringLiteral("gammagroup"));
        // x/y/w/h all default to 0; (0,0,0,0) signals "use full image".
        bool ok = false;
        const int x = el.attrs.value(QStringLiteral("x")).toInt(&ok);
        const int y = el.attrs.value(QStringLiteral("y")).toInt();
        const int w = el.attrs.value(QStringLiteral("w")).toInt();
        const int h = el.attrs.value(QStringLiteral("h")).toInt();
        d.srcRect = QRect(x, y, w, h);
        if (!d.id.isEmpty() && !d.file.isEmpty())
            out.insert(d.id, d);
    }
    for (const auto &c : el.children) collectBitmaps(c, out);
}
}  // namespace

int BitmapRegistry::loadFromDocument(const SkinXml::Document &doc) {
    m_skinDir = doc.skinDir;
    m_defs.clear();
    m_imgCache.clear();
    m_subCache.clear();
    collectBitmaps(doc.root, m_defs);
    return m_defs.size();
}

void BitmapRegistry::setGammasetRegistry(GammasetRegistry *gs) {
    m_gammasets = gs;
    m_subCache.clear();   // existing tinted images become stale
    m_maskedCache.clear();
}

void BitmapRegistry::setChromeCutouts(
        const QHash<QString, QList<ChromeCutout>> &cutouts) {
    m_chromeCutouts = cutouts;
    m_maskedCache.clear();
}

QImage BitmapRegistry::chromeImageFor(const QString &id) {
    auto cIt = m_chromeCutouts.constFind(id);
    if (cIt == m_chromeCutouts.constEnd() || cIt.value().isEmpty())
        return imageFor(id);
    auto mIt = m_maskedCache.constFind(id);
    if (mIt != m_maskedCache.constEnd()) return mIt.value();
    QImage base = imageFor(id);
    if (base.isNull()) return base;
    QImage masked = base.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    {
        QPainter p(&masked);
        p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        p.setRenderHint(QPainter::Antialiasing,          false);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        for (const ChromeCutout &cc : cIt.value()) {
            QImage cut = imageFor(cc.cutoutImage);
            if (!cut.isNull()) p.drawImage(cc.offset, cut);
        }
    }
    m_maskedCache.insert(id, masked);
    return masked;
}

const BitmapDef *BitmapRegistry::find(const QString &id) const {
    auto it = m_defs.constFind(id);
    return it == m_defs.constEnd() ? nullptr : &it.value();
}

QImage BitmapRegistry::imageFor(const QString &id) {
    auto it = m_defs.constFind(id);
    if (it == m_defs.constEnd()) return {};
    if (m_gammasets) {
        // Tinted-sub-rect cache so we don't repeat the per-pixel
        // transform on every paint.
        auto sIt = m_subCache.constFind(id);
        if (sIt != m_subCache.constEnd()) return sIt.value();
        QImage out = imageFor(it.value());
        if (!out.isNull() && !it.value().gammagroup.isEmpty()) {
            const auto t = m_gammasets->transformFor(it.value().gammagroup);
            GammasetRegistry::applyToImage(out, t);
        }
        m_subCache.insert(id, out);
        return out;
    }
    return imageFor(it.value());
}

QImage BitmapRegistry::imageFor(const BitmapDef &def) {
    // Resolution order:
    //   1. Local override — the same relative path INSIDE the current
    //      skin.  WinampModernPP / Big Bento override their parent
    //      skin's assets by dropping replacement files at the same
    //      relative subpath; the XML still references the parent via
    //      `../Parent Skin/...` so the local copy is the intended
    //      override.  Strip leading `../<dir>/` segments to get the
    //      "shared-suffix" subpath, then try that in m_skinDir first.
    //   2. As declared (sibling-skin or absolute reference).
    //   3. Case-insensitive search next to either of the above.
    QString localOverride;
    {
        QString rel = def.file;
        while (rel.startsWith(QStringLiteral("../"))) {
            int slash = rel.indexOf(QChar('/'), 3);
            if (slash < 0) break;
            rel = rel.mid(slash + 1);
        }
        if (rel != def.file) {
            localOverride = QDir(m_skinDir).filePath(rel);
        }
    }
    QString resolved = QDir(m_skinDir).filePath(def.file);

    auto cIt = m_imgCache.constFind(resolved);
    QImage whole;
    if (cIt != m_imgCache.constEnd()) {
        whole = cIt.value();
    } else {
        // Try the local override first.
        if (!localOverride.isEmpty()) {
            QImageReader r(localOverride);
            whole = r.read();
        }
        if (whole.isNull()) {
            QImageReader r(resolved);
            whole = r.read();
        }
        if (whole.isNull()) {
            // Case-insensitive search in BOTH the local-override
            // parent dir and the declared parent dir.
            auto tryDir = [&](const QString &path) {
                if (!whole.isNull()) return;
                QFileInfo asked(path);
                QDir d(asked.absoluteDir());
                const QString want = asked.fileName().toLower();
                for (const auto &entry : d.entryInfoList(QDir::Files)) {
                    if (entry.fileName().toLower() == want) {
                        QImageReader r2(entry.absoluteFilePath());
                        whole = r2.read();
                        break;
                    }
                }
            };
            if (!localOverride.isEmpty()) tryDir(localOverride);
            tryDir(resolved);
        }
        m_imgCache.insert(resolved, whole);
    }
    if (whole.isNull()) return {};

    QRect rect = def.srcRect;
    if (rect.width() <= 0 && rect.height() <= 0 && rect.x() == 0 && rect.y() == 0) {
        // Whole-image bitmap.
        return whole;
    }
    // Negative w/h means "image dimension minus the absolute".
    if (rect.width()  <= 0) rect.setWidth (whole.width()  - rect.x() + rect.width());
    if (rect.height() <= 0) rect.setHeight(whole.height() - rect.y() + rect.height());
    rect = rect.intersected(whole.rect());
    if (rect.isEmpty()) return {};
    return whole.copy(rect);
}

}  // namespace WasabiQt
