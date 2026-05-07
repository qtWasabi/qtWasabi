// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/GammasetRegistry.h>
#include <WasabiQt/SkinXml.h>

#include <QDir>
#include <QImageReader>

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
    // Resolve the file (case-fixup: skins on Windows are lax about
    // case, but the Linux filesystem isn't).  Look for the file as
    // declared first; if missing, do a case-insensitive search next
    // to it.
    QString resolved = QDir(m_skinDir).filePath(def.file);

    auto cIt = m_imgCache.constFind(resolved);
    QImage whole;
    if (cIt != m_imgCache.constEnd()) {
        whole = cIt.value();
    } else {
        QImageReader r(resolved);
        whole = r.read();
        if (whole.isNull()) {
            // Case-insensitive search in the parent dir.
            QFileInfo asked(resolved);
            QDir d(asked.absoluteDir());
            const QString want = asked.fileName().toLower();
            for (const auto &entry : d.entryInfoList(QDir::Files)) {
                if (entry.fileName().toLower() == want) {
                    QImageReader r2(entry.absoluteFilePath());
                    whole = r2.read();
                    break;
                }
            }
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
