// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/LayerPainter.h>
#include <WasabiQt/BitmapRegistry.h>

#include <QImage>
#include <QPainter>
#include <QRect>
#include <QSize>

namespace WasabiQt::LayerPainter {

namespace {
int attrInt(const QHash<QString, QString> &a,
            const QString &key, int defVal = 0) {
    auto it = a.constFind(key);
    if (it == a.constEnd()) return defVal;
    bool ok = false;
    const int v = it.value().toInt(&ok);
    return ok ? v : defVal;
}
bool attrBool(const QHash<QString, QString> &a, const QString &key) {
    return a.value(key) == QStringLiteral("1");
}
}  // namespace

bool paintLayer(QPainter *p, BitmapRegistry &reg,
                const QHash<QString, QString> &attrs,
                const QSize &containerSize) {
    const QString image = attrs.value(QStringLiteral("image"));
    if (image.isEmpty()) return false;
    QImage src = reg.imageFor(image);
    if (src.isNull()) return false;

    int x = attrInt(attrs, QStringLiteral("x"));
    int y = attrInt(attrs, QStringLiteral("y"));
    int w = attrInt(attrs, QStringLiteral("w"), 0);
    int h = attrInt(attrs, QStringLiteral("h"), 0);

    if (attrBool(attrs, QStringLiteral("relatw"))) w = containerSize.width()  + w;
    if (attrBool(attrs, QStringLiteral("relath"))) h = containerSize.height() + h;
    if (w <= 0) w = src.width();
    if (h <= 0) h = src.height();

    p->drawImage(QRect(x, y, w, h), src);
    return true;
}

}  // namespace WasabiQt::LayerPainter
