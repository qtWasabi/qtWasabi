// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/TextPainter.h>
#include <WasabiQt/FontRegistry.h>
#include <WasabiQt/BitmapRegistry.h>

#include <QImage>
#include <QPainter>
#include <QRect>
#include <QSize>
#include <QString>

namespace WasabiQt::TextPainter {

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
    auto it = a.constFind(key);
    if (it == a.constEnd()) return false;
    const QString &v = it.value();
    return v == QStringLiteral("1") ||
           v.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
}

}  // namespace

bool paintText(QPainter *p,
               FontRegistry &fontReg, BitmapRegistry &bmpReg,
               const QHash<QString, QString> &attrs,
               const QSize &containerSize,
               const DisplayResolver &resolver) {
    const QString fontId = attrs.value(QStringLiteral("font"));
    if (fontId.isEmpty()) return false;

    const auto *fontDef = fontReg.find(fontId);
    if (!fontDef) {
        // TrueType-style font name (e.g. "Arial").  Skipped at this
        // milestone — would need a font-loader.  Fail silently so
        // the rest of the layout keeps painting.
        return false;
    }

    // Resolve x/y/w/h with relat* against the container.
    int x = attrInt(attrs, QStringLiteral("x"));
    int y = attrInt(attrs, QStringLiteral("y"));
    int w = attrInt(attrs, QStringLiteral("w"), 0);
    int h = attrInt(attrs, QStringLiteral("h"), 0);
    if (attrBool(attrs, QStringLiteral("relatx"))) x = containerSize.width()  + x;
    if (attrBool(attrs, QStringLiteral("relaty"))) y = containerSize.height() + y;
    if (attrBool(attrs, QStringLiteral("relatw"))) w = containerSize.width()  + w;
    if (attrBool(attrs, QStringLiteral("relath"))) h = containerSize.height() + h;
    if (w <= 0) w = fontDef->charWidth * 8;     // arbitrary fallback
    if (h <= 0) h = fontDef->charHeight;

    // Pick the string to paint: resolver(display=) → default → empty.
    QString text;
    const QString display = attrs.value(QStringLiteral("display"));
    if (resolver && !display.isEmpty()) text = resolver(display);
    if (text.isEmpty()) text = attrs.value(QStringLiteral("default"));
    if (text.isEmpty()) return true;             // nothing to draw

    if (attrBool(attrs, QStringLiteral("forceuppercase")))
        text = text.toUpper();

    // Measure the line we want to draw, accounting for hSpacing.
    int lineW = 0;
    for (int i = 0; i < text.size(); ++i) {
        if (i > 0) lineW += fontDef->hSpacing;
        lineW += fontDef->charWidth;
    }

    // Alignment within the (x,y,w,h) rect.
    int drawX = x;
    const QString align = attrs.value(QStringLiteral("align"))
                              .toLower();
    if (align == QStringLiteral("center"))
        drawX = x + (w - lineW) / 2;
    else if (align == QStringLiteral("right"))
        drawX = x + (w - lineW);
    int drawY = y + (h - fontDef->charHeight) / 2;
    if (drawY < y) drawY = y;

    // Paint each glyph.
    int cx = drawX;
    for (int i = 0; i < text.size(); ++i) {
        QImage glyph = fontReg.glyph(fontId, text.at(i), bmpReg);
        if (!glyph.isNull()) {
            p->drawImage(QPoint(cx, drawY), glyph);
        }
        cx += fontDef->charWidth + fontDef->hSpacing;
    }
    return true;
}

}  // namespace WasabiQt::TextPainter
