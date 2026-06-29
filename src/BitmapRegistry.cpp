// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <qtWasabi/BitmapRegistry.h>
#include <qtWasabi/GammasetRegistry.h>
#include <qtWasabi/SkinXml.h>
#include <qtWasabi/Widget.h>

#include <QDir>
#include <QImageReader>
#include <QPainter>

#include <functional>

namespace qtWasabi {

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
        // Parse via toDouble()->truncate so fractional sprite coords don't
        // collapse to 0 (toInt rejects decimals).  Engine-wide coord rule.
        const int x = static_cast<int>(el.attrs.value(QStringLiteral("x")).toDouble());
        const int y = static_cast<int>(el.attrs.value(QStringLiteral("y")).toDouble());
        const int w = static_cast<int>(el.attrs.value(QStringLiteral("w")).toDouble());
        const int h = static_cast<int>(el.attrs.value(QStringLiteral("h")).toDouble());
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
    m_satMaskedIds.clear();
    m_accentRegions.clear();
    m_regionGroups.clear();
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

namespace {
// Last-resort role guess from a bitmap id's wording, for bitmaps no
// typed widget referenced.  Order matters: a "playbutton" is a button
// before it is anything else.
QString roleFromId(const QString &idIn) {
    const QString id = idIn.toLower();
    auto has = [&](const char *k) { return id.contains(QLatin1String(k)); };
    if (has("button") || has("btn") || has("play") || has("stop") ||
        has("pause") || has("next") || has("prev") || has("eject") ||
        has("repeat") || has("shuffle"))
        return QStringLiteral("syn.button");
    if (has("slider") || has("thumb") || has("knob") || has("volume") ||
        has("balance") || has("seek") || has("posbar"))
        return QStringLiteral("syn.slider");
    if (has("vis") || has("spectrum") || has("analyzer") || has("osc") ||
        has("eq"))
        return QStringLiteral("syn.vis");
    if (has("display") || has("screen") || has("lcd") || has("number") ||
        has("digit") || has("time") || has("text") || has("font"))
        return QStringLiteral("syn.display");
    if (has("title") || has("bg") || has("background") || has("body") ||
        has("face") || has("head") || has("window") || has("frame") ||
        has("panel") || has("drawer") || has("component"))
        return QStringLiteral("syn.background");
    return QStringLiteral("syn.other");
}

// The role implied by a widget's tag.  Empty when the tag carries no
// bitmaps of interest (the id-keyword guess covers those instead).
QString roleForTag(const QString &tag) {
    if (tag == QLatin1String("button") ||
        tag == QLatin1String("togglebutton"))
        return QStringLiteral("syn.button");
    if (tag == QLatin1String("layer") ||
        tag == QLatin1String("animatedlayer"))
        return QStringLiteral("syn.background");
    if (tag == QLatin1String("slider") ||
        tag == QLatin1String("seekbar") || tag == QLatin1String("eqband"))
        return QStringLiteral("syn.slider");
    if (tag == QLatin1String("vis") || tag == QLatin1String("eqvis") ||
        tag == QLatin1String("visualization"))
        return QStringLiteral("syn.vis");
    if (tag == QLatin1String("text") || tag == QLatin1String("status"))
        return QStringLiteral("syn.text");
    return QString();
}
}  // namespace

void BitmapRegistry::assignRolesFromWidgetTree(const Widget &root,
                                              bool accentDrawers) {
    // The bitmap-referencing attribute slots across the widget set.
    static const char *kSlots[] = {
        "image", "downimage", "hoverimage", "activeimage",
        "thumb", "downthumb", "hoverthumb",
        "barleft", "barmiddle", "barright", "left", "middle", "right",
    };
    QHash<QString, QString> roleById;     // strongest signal first
    std::function<void(const Widget &)> walk = [&](const Widget &w) {
        const QString role = roleForTag(w.tag);
        if (!role.isEmpty()) {
            for (const char *slot : kSlots) {
                const QString id = w.attrs.value(QLatin1String(slot));
                if (!id.isEmpty() && !roleById.contains(id))
                    roleById.insert(id, role);
            }
        }
        for (const auto &c : w.children) if (c) walk(*c);
    };
    walk(root);

    // Anything the widget tree didn't reach falls back to the id wording.
    // Then tag every still-untagged bitmap def — never overwrite a real
    // gammagroup the skin author declared.
    int tagged = 0;
    for (auto it = m_defs.begin(); it != m_defs.end(); ++it) {
        if (!it->gammagroup.isEmpty()) continue;
        auto r = roleById.constFind(it.key());
        it->gammagroup = (r != roleById.constEnd()) ? r.value()
                                                     : roleFromId(it.key());
        ++tagged;
    }
    // Optional (theme-less skins only): give the SEEK / position indicator
    // the theme accent — a small always-visible spot of colour, like the
    // bright accent on a real Winamp.  Overriding a real gammagroup is safe
    // here because no native theme of this skin relies on it.
    if (accentDrawers) {
        for (auto it = m_defs.begin(); it != m_defs.end(); ++it) {
            const QString id = it.key().toLower();
            // The body/chrome ("Backgrounds") takes the theme's body colour.
            // The drawer panels take it too, BUT saturation-masked, so only
            // the coloured panel recolours while the SILVER speaker cones are
            // left as-is.  Only for theme-less skins.
            if (it->gammagroup.compare(QStringLiteral("Backgrounds"),
                                       Qt::CaseInsensitive) == 0) {
                it->gammagroup = QStringLiteral("syn.body");
                if (id.contains(QStringLiteral("drawer")))
                    m_satMaskedIds.insert(it.key());
            }
        }
    }
    m_subCache.clear();   // re-tint with the freshly assigned roles
    if (qEnvironmentVariableIntValue("WASABIQT_TRACE_MAKI") == 1)
        fprintf(stderr, "[synrole] tagged %d untagged bitmaps with roles\n",
                tagged);
}

void BitmapRegistry::setAccentRegion(const QString &id, const QRect &rect,
                                    QRgb color, int threshold) {
    m_accentRegions.insert(id, AccentRegion{rect, color, threshold});
    m_subCache.remove(id);
}

void BitmapRegistry::setRegionGroup(const QString &id, const QRect &rect,
                                    const QString &gammagroup, int threshold) {
    m_regionGroups[id].append(RegionGroup{rect, gammagroup, threshold});
    m_subCache.remove(id);
}

void BitmapRegistry::clearAccentRegions() {
    for (auto it = m_accentRegions.constBegin(); it != m_accentRegions.constEnd(); ++it)
        m_subCache.remove(it.key());
    m_accentRegions.clear();
    for (auto it = m_regionGroups.constBegin(); it != m_regionGroups.constEnd(); ++it)
        m_subCache.remove(it.key());
    m_regionGroups.clear();
}

namespace {
// Recolour only the bright pixels of `a.rect` to `a.color` — picks a logo
// shape out of the larger bitmap it's painted into, without colouring the
// rectangle around it.
void applyAccentRegion(QImage &img, const QImage &mask, const QRect &rect,
                       QRgb color, int threshold) {
    if (img.isNull() || mask.isNull()) return;
    if (img.format() != QImage::Format_ARGB32 &&
        img.format() != QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_ARGB32);
    const QRect r = rect.intersected(img.rect()).intersected(mask.rect());
    for (int y = r.top(); y <= r.bottom(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = r.left(); x <= r.right(); ++x) {
            const QRgb m = mask.pixel(x, y);   // untinted source decides the shape
            if (qAlpha(m) < 40) continue;
            const int lum = (qRed(m) * 299 + qGreen(m) * 587 +
                             qBlue(m) * 114) / 1000;
            if (lum >= threshold)
                line[x] = qRgba(qRed(color), qGreen(color),
                                qBlue(color), qAlpha(line[x]));
        }
    }
}

// Re-tint only the bright pixels of `rect` with transform `t`, taking the
// source colour from the UNTINTED `orig` so the glyphs read the same as a
// sibling bitmap themed by the same role.  Non-bright pixels (the panel
// behind the glyphs) keep whatever the bitmap's own group already produced.
void applyRegionGroup(QImage &img, const QImage &orig, const QRect &rect,
                      const GammaGroup &t, int threshold) {
    if (img.isNull() || orig.isNull()) return;
    if (img.format() != QImage::Format_ARGB32 &&
        img.format() != QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_ARGB32);
    QImage tinted = orig.convertToFormat(QImage::Format_ARGB32);
    GammasetRegistry::applyToImage(tinted, t);
    const QRect r = rect.intersected(img.rect()).intersected(orig.rect());
    for (int y = r.top(); y <= r.bottom(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = r.left(); x <= r.right(); ++x) {
            const QRgb o = orig.pixel(x, y);   // untinted source decides the shape
            if (qAlpha(o) < 40) continue;
            const int lum = (qRed(o) * 299 + qGreen(o) * 587 +
                             qBlue(o) * 114) / 1000;
            if (lum < threshold) continue;
            const QRgb tp = tinted.pixel(x, y);
            line[x] = qRgba(qRed(tp), qGreen(tp), qBlue(tp), qAlpha(line[x]));
        }
    }
}
}  // namespace

QStringList BitmapRegistry::usedGammagroups() const {
    QStringList out;
    for (auto it = m_defs.constBegin(); it != m_defs.constEnd(); ++it)
        if (!it->gammagroup.isEmpty() && !out.contains(it->gammagroup))
            out.append(it->gammagroup);
    return out;
}

QImage BitmapRegistry::imageFor(const QString &id) {
    auto it = m_defs.constFind(id);
    if (qEnvironmentVariableIntValue("WASABIQT_TRACE_BITMAP") == 1 &&
        (id.startsWith(QStringLiteral("infocomp")) ||
         id.startsWith(QStringLiteral("window.titlebar")))) {
        if (it == m_defs.constEnd()) {
            fprintf(stderr, "[bitmap-id] %s NOT REGISTERED\n",
                    id.toLocal8Bit().constData());
        } else {
            fprintf(stderr, "[bitmap-id] %s file=%s rect=%dx%d@(%d,%d)\n",
                    id.toLocal8Bit().constData(),
                    it.value().file.toLocal8Bit().constData(),
                    it.value().srcRect.width(),
                    it.value().srcRect.height(),
                    it.value().srcRect.x(),
                    it.value().srcRect.y());
        }
    }
    if (it == m_defs.constEnd()) {
        // Skin XML didn't declare this id — fall through to the
        // runtime registry (plugin-supplied bitmaps).  Decoded-
        // already path first, deferred-path-load second.
        auto rit = m_runtime.constFind(id);
        if (rit != m_runtime.constEnd()) return rit.value();
        auto pit = m_runtimePaths.constFind(id);
        if (pit != m_runtimePaths.constEnd()) {
            QImage decoded(pit.value());
            if (!decoded.isNull()) {
                // Promote to the decoded map so re-queries skip
                // the disk hit.
                m_runtime.insert(id, decoded);
                return decoded;
            }
        }
        return {};
    }
    if (m_gammasets) {
        // Tinted-sub-rect cache so we don't repeat the per-pixel
        // transform on every paint.
        auto sIt = m_subCache.constFind(id);
        if (sIt != m_subCache.constEnd()) return sIt.value();
        QImage out = imageFor(it.value());
        // Keep the untinted pixels as the mask source for any accent region,
        // so the bolt is isolated by ITS brightness, not the body tint's.
        auto aIt = m_accentRegions.constFind(id);
        auto gIt = m_regionGroups.constFind(id);
        const bool wantOrig = (aIt != m_accentRegions.constEnd() ||
                               gIt != m_regionGroups.constEnd());
        const QImage orig =
            (wantOrig && !out.isNull()) ? out.copy() : QImage();
        if (!out.isNull()) {
            // Per-gammagroup tint (skins that ship Color Themes tag their
            // bitmaps; this honours the active theme's transform for the
            // tag).
            if (!it.value().gammagroup.isEmpty()) {
                const auto t = m_gammasets->transformFor(it.value().gammagroup);
                const int chromaMin = m_satMaskedIds.contains(id) ? 32 : 0;
                GammasetRegistry::applyToImage(out, t, chromaMin);
            }
            // Global recolor: a synthetic theme tints EVERY bitmap, even
            // ones the skin left untagged — this is what lets a closed
            // skin be recoloured.  applyToImage no-ops on identity, so a
            // normal per-group theme costs nothing here.
            GammasetRegistry::applyToImage(out, m_gammasets->globalTransform());
            // Region tinted by a different role (e.g. a label baked into the
            // body panel that should match a sibling button).
            if (gIt != m_regionGroups.constEnd())
                for (const RegionGroup &rg : *gIt)
                    applyRegionGroup(out, orig, rg.rect,
                                     m_gammasets->transformFor(rg.group),
                                     rg.threshold);
            // Masked accent region (e.g. a logo baked into the body bitmap).
            if (aIt != m_accentRegions.constEnd())
                applyAccentRegion(out, orig, aIt->rect, aIt->color,
                                  aIt->threshold);
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

    const bool trace = qEnvironmentVariableIntValue(
        "WASABIQT_TRACE_BITMAP") == 1 &&
        def.file.contains(QStringLiteral("window/window.png"));
    if (trace) {
        fprintf(stderr, "[bitmap-def] file=%s resolved=%s localOverride=%s\n",
                def.file.toLocal8Bit().constData(),
                resolved.toLocal8Bit().constData(),
                localOverride.toLocal8Bit().constData());
    }
    auto cIt = m_imgCache.constFind(resolved);
    QImage whole;
    if (cIt != m_imgCache.constEnd()) {
        whole = cIt.value();
        if (trace) fprintf(stderr, "  cache HIT (whole=%dx%d isNull=%d)\n",
                            whole.width(), whole.height(),
                            whole.isNull() ? 1 : 0);
    } else {
        // Try the path AS DECLARED first.  The skin author explicitly
        // wrote `file="../Big Bento/window/window.png"`, meaning the
        // parent skin's file is the intended source.  Falling back to
        // a local-override sibling (same suffix path in this skin) is
        // only a last resort — Bento ships its OWN window.png as a
        // smaller 193x40 file used by a different set of bitmaps, and
        // letting that pre-empt the parent file corrupts every srcRect
        // that referenced the larger 248x102 Big Bento file.
        {
            QImageReader r(resolved);
            whole = r.read();
            if (qEnvironmentVariableIntValue("WASABIQT_TRACE_BITMAP") == 1 &&
                whole.isNull()) {
                fprintf(stderr, "[bitmap] failed: %s (file=%s) reader=%s\n",
                        resolved.toLocal8Bit().constData(),
                        def.file.toLocal8Bit().constData(),
                        r.errorString().toLocal8Bit().constData());
            }
        }
        if (whole.isNull() && !localOverride.isEmpty()) {
            QImageReader r(localOverride);
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

// ── Runtime registration ────────────────────────────────────────
//
// Plugins (ported gen_ml + ml_* modules) ship their resource
// bitmaps as either decoded QImages or paths on disk.  Either gets
// surfaced through the same `imageFor(id)` lookup TreePainter /
// widget code already uses — no per-call-site branching needed.

bool BitmapRegistry::registerRuntimeBitmap(const QString &id,
                                             const QImage &img) {
    if (id.isEmpty() || img.isNull()) return false;
    m_runtime.insert(id, img);
    // If a previously-deferred path entry exists for the same id,
    // drop it — the decoded version supersedes.
    m_runtimePaths.remove(id);
    // Invalidate any cached tinted sub-rect so the next imageFor
    // call sees the new pixels.
    m_subCache.remove(id);
    return true;
}

bool BitmapRegistry::registerRuntimeBitmap(const QString &id,
                                             const QString &filePath) {
    if (id.isEmpty() || filePath.isEmpty()) return false;
    m_runtimePaths.insert(id, filePath);
    m_runtime.remove(id);   // force re-decode on next lookup
    m_subCache.remove(id);
    return true;
}

void BitmapRegistry::unregisterRuntimeBitmap(const QString &id) {
    m_runtime.remove(id);
    m_runtimePaths.remove(id);
    m_subCache.remove(id);
}

}  // namespace qtWasabi
