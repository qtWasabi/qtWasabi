// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/TreePainter.h>
#include <WasabiQt/Layout.h>
#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/FontRegistry.h>
#include <WasabiQt/LayerPainter.h>
#include <WasabiQt/TextPainter.h>

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

    if (t == QStringLiteral("text")) {
        TextPainter::paintText(p, *ctx.font, *ctx.bmp, node.attrs,
                               canvas, ctx.resolver);
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

    // <slider>, <vis>, <albumart>, ... — painted in later milestones.
    for (const auto &child : node.children)
        paintRecursive(p, child, ctx, canvas);
}

}  // namespace

void paintTree(QPainter *p, const ResolvedWidget &root,
               BitmapRegistry &reg, FontRegistry &fontReg,
               const QSize &canvas, const DisplayResolver &resolver) {
    PaintCtx ctx{&reg, &fontReg, resolver};
    paintRecursive(p, root, ctx, canvas);
}

}  // namespace WasabiQt::TreePainter
