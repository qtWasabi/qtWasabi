// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/TreePainter.h>
#include <WasabiQt/Layout.h>
#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/FontRegistry.h>
#include <WasabiQt/Host.h>
#include <WasabiQt/LayerPainter.h>
#include <WasabiQt/TextPainter.h>
#include <QImage>

#include <QHash>
#include <QPainter>
#include <QRect>
#include <QSize>
#include <QString>

namespace WasabiQt::TreePainter {

namespace {

using Layout::ResolvedWidget;

int attrInt(const QHash<QString, QString> &a,
            const QString &key, int defVal = 0) {
    auto it = a.constFind(key);
    if (it == a.constEnd()) return defVal;
    bool ok = false;
    const int v = it.value().toInt(&ok);
    return ok ? v : defVal;
}
bool attrBool(const QHash<QString, QString> &a, const QString &key) {
    auto it = a.constFind(key);
    if (it == a.constEnd()) return false;
    const QString &v = it.value();
    return v == QStringLiteral("1") || v.compare(QStringLiteral("true"),
                                                  Qt::CaseInsensitive) == 0;
}

// Resolve a widget's pixel rect against its parent's size.  Mirrors
// LayerPainter's logic — relatx/relaty against parent edge, relatw/h
// against parent dimension.  When w/h is unset (==0), defer to the
// caller (zero indicates "natural / full" depending on widget kind).
QRect resolveRect(const QHash<QString, QString> &a, const QSize &parent) {
    int x = attrInt(a, QStringLiteral("x"));
    int y = attrInt(a, QStringLiteral("y"));
    int w = attrInt(a, QStringLiteral("w"), 0);
    int h = attrInt(a, QStringLiteral("h"), 0);
    if (attrBool(a, QStringLiteral("relatx"))) x = parent.width()  + x;
    if (attrBool(a, QStringLiteral("relaty"))) y = parent.height() + y;
    if (attrBool(a, QStringLiteral("relatw"))) w = parent.width()  + w;
    if (attrBool(a, QStringLiteral("relath"))) h = parent.height() + h;
    return QRect(x, y, w, h);
}

// Pull the painted-state bitmap id for buttons/togglebuttons.
// For M7 we only render the normal-state image; downImage/hoverImage
// are input-time decisions the embedder makes later.
QString staticImageId(const QHash<QString, QString> &a) {
    return a.value(QStringLiteral("image"));
}

struct PaintCtx {
    BitmapRegistry        *bmp;
    FontRegistry          *font;
    DisplayResolver        resolver;
    Host                  *host = nullptr;
};

void paintRecursive(QPainter *p, const ResolvedWidget &node,
                    PaintCtx &ctx, const QSize &canvas) {
    if (node.attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;

    const QString &t = node.tag;

    if (t == QStringLiteral("layer")) {
        LayerPainter::paintLayer(p, *ctx.bmp, node.attrs, canvas);
        return;
    }

    if (t == QStringLiteral("button") || t == QStringLiteral("togglebutton")) {
        QHash<QString, QString> a = node.attrs;
        a.insert(QStringLiteral("image"), staticImageId(a));
        LayerPainter::paintLayer(p, *ctx.bmp, a, canvas);
        return;
    }

    if (t == QStringLiteral("text") || t == QStringLiteral("songticker")) {
        // <songticker> paints like <text> with no scrolling; the
        // displayed string comes through the resolver as
        // "songtitle" / "songinfo" or whatever the embedder wires up.
        QHash<QString, QString> a = node.attrs;
        if (t == QStringLiteral("songticker") &&
            a.value(QStringLiteral("display")).isEmpty()) {
            a.insert(QStringLiteral("display"),
                     QStringLiteral("songtitle"));
        }
        TextPainter::paintText(p, *ctx.font, *ctx.bmp, a, canvas,
                               ctx.resolver);
        return;
    }

    if (t == QStringLiteral("vis")) {
        // Spectrum-style bars.  Real FFT-driven spectrum is M11+
        // work; for now bar heights are pseudo-random per band but
        // multiplied by Host::audioLevel() (recent RMS) so the
        // whole visualisation bounces with the audio.  No host =
        // 0-amplitude bars (chrome stays visible but doesn't move).
        const QRect r = resolveRect(node.attrs, canvas);
        if (r.width() > 0 && r.height() > 0) {
            QColor band(255, 255, 255);
            const QString c1 = node.attrs.value(QStringLiteral("colorband1"));
            if (c1.contains(QChar(','))) {
                const auto parts = c1.split(QChar(','));
                if (parts.size() == 3)
                    band = QColor(parts[0].toInt(), parts[1].toInt(),
                                  parts[2].toInt());
            }
            const double level = ctx.host
                ? qBound(0.0, ctx.host->audioLevel() * 4.0, 1.0)
                : 0.0;
            const int barCount = 16;
            const int barW = r.width() / barCount;
            const int maxH = r.height() - 4;
            for (int i = 0; i < barCount; ++i) {
                const int rawH = 4 + ((i * 17 + 3) % maxH);
                const int h = qMax(1, int(rawH * level));
                p->fillRect(r.x() + i * barW + 1,
                            r.y() + (r.height() - h),
                            barW - 1, h, band);
            }
        }
        return;
    }

    // Containers paint their children with a translated origin.
    if (t == QStringLiteral("group")     ||
        t == QStringLiteral("container") ||
        t == QStringLiteral("layout")    ||
        t == QStringLiteral("groupdef")) {
        const QRect r = resolveRect(node.attrs, canvas);
        QSize childSize = canvas;
        if (r.width()  > 0) childSize.setWidth (r.width());
        if (r.height() > 0) childSize.setHeight(r.height());
        const bool translate = (r.x() != 0 || r.y() != 0)
                               && t != QStringLiteral("layout");
        if (translate) p->save(), p->translate(r.x(), r.y());
        for (const auto &child : node.children)
            paintRecursive(p, child, ctx, childSize);
        if (translate) p->restore();
        return;
    }

    if (t == QStringLiteral("slider")) {
        const QRect r = resolveRect(node.attrs, canvas);
        if (r.width() <= 0 || r.height() <= 0) return;
        // Optional background bitmap (most Modern-skin sliders
        // paint their groove via separate <layer> siblings; some
        // do carry an `image=` attr though).
        const QString bgId = node.attrs.value(QStringLiteral("image"));
        if (!bgId.isEmpty()) {
            QHash<QString, QString> bg = node.attrs;
            bg.remove(QStringLiteral("thumb"));
            LayerPainter::paintLayer(p, *ctx.bmp, bg, canvas);
        }
        const QString thumbId = node.attrs.value(QStringLiteral("thumb"));
        if (thumbId.isEmpty() || !ctx.host) return;
        const QString action = node.attrs.value(QStringLiteral("action"));
        const double pos = ctx.host->sliderPosition(action);
        if (pos < 0.0) return;                    // unknown action
        QImage thumb = ctx.bmp->imageFor(thumbId);
        if (thumb.isNull()) return;
        const int travel = qMax(0, r.width() - thumb.width());
        const int thumbX = r.x() + int(pos * travel);
        const int thumbY = r.y() + (r.height() - thumb.height()) / 2;
        p->drawImage(thumbX, thumbY, thumb);
        return;
    }

    // <vis>, <albumart>, ... — painted in later milestones.
    for (const auto &child : node.children)
        paintRecursive(p, child, ctx, canvas);
}

}  // namespace

void paintTree(QPainter *p, const ResolvedWidget &root,
               BitmapRegistry &reg, FontRegistry &fontReg,
               const QSize &canvas, const DisplayResolver &resolver) {
    PaintCtx ctx{&reg, &fontReg, resolver, nullptr};
    paintRecursive(p, root, ctx, canvas);
}

void paintTree(QPainter *p, const ResolvedWidget &root,
               BitmapRegistry &reg, FontRegistry &fontReg,
               const QSize &canvas, Host *host) {
    PaintCtx ctx{&reg, &fontReg, makeDefaultDisplayResolver(host), host};
    paintRecursive(p, root, ctx, canvas);
}

}  // namespace WasabiQt::TreePainter
