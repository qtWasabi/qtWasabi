// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/TextPainter.h>
#include <WasabiQt/FontRegistry.h>
#include <WasabiQt/BitmapRegistry.h>

#include <QColor>
#include <QFont>
#include <QFontMetrics>
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
    const bool isBitmap = (fontDef != nullptr);
    if (!isBitmap) {
        // TrueType fall-through — handled at the end of this function.
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
    if (w <= 0) w = isBitmap ? (fontDef->charWidth * 8) : 64;
    if (h <= 0) h = isBitmap ? fontDef->charHeight     : 16;

    // Pick the string: resolver(display=) → default/text → empty.
    QString text;
    const QString display = attrs.value(QStringLiteral("display"));
    if (resolver && !display.isEmpty()) text = resolver(display);
    if (text.isEmpty()) text = attrs.value(QStringLiteral("default"));
    if (text.isEmpty()) text = attrs.value(QStringLiteral("text"));
    if (text.isEmpty()) return true;

    if (attrBool(attrs, QStringLiteral("forceuppercase")))
        text = text.toUpper();

    const QString align = attrs.value(QStringLiteral("align")).toLower();

    if (isBitmap) {
        // ── bitmap font path ───────────────────────────────────
        int lineW = 0;
        for (int i = 0; i < text.size(); ++i) {
            if (i > 0) lineW += fontDef->hSpacing;
            lineW += fontDef->charWidth;
        }
        int drawX = x;
        if      (align == QStringLiteral("center")) drawX = x + (w - lineW) / 2;
        else if (align == QStringLiteral("right"))  drawX = x + (w - lineW);
        int drawY = y + (h - fontDef->charHeight) / 2;
        if (drawY < y) drawY = y;
        int cx = drawX;
        for (int i = 0; i < text.size(); ++i) {
            QImage glyph = fontReg.glyph(fontId, text.at(i), bmpReg);
            if (!glyph.isNull())
                p->drawImage(QPoint(cx, drawY), glyph);
            cx += fontDef->charWidth + fontDef->hSpacing;
        }
        return true;
    }

    // ── TrueType fallback ──────────────────────────────────────
    // Wasabi's `font="Arial"` etc. — render via QPainter::drawText.
    // Honours fontsize, bold, italic, color (best-effort string→QColor),
    // align, antialias.
    QFont qf(fontId);
    const int fontsize = attrInt(attrs, QStringLiteral("fontsize"), 12);
    qf.setPixelSize(fontsize);
    if (attrBool(attrs, QStringLiteral("bold")))   qf.setBold(true);
    if (attrBool(attrs, QStringLiteral("italic"))) qf.setItalic(true);

    p->save();
    p->setFont(qf);
    if (attrBool(attrs, QStringLiteral("antialias")))
        p->setRenderHint(QPainter::TextAntialiasing, true);

    // Color: accepted forms are "r,g,b" or a named id (skipped — would
    // need a color registry).  Default white so something shows.
    const QString colorStr = attrs.value(QStringLiteral("color"));
    QColor color(Qt::white);
    if (colorStr.contains(QChar(','))) {
        const auto parts = colorStr.split(QChar(','));
        if (parts.size() == 3)
            color = QColor(parts[0].toInt(), parts[1].toInt(), parts[2].toInt());
    }
    p->setPen(color);

    int qFlag = Qt::AlignVCenter;
    if      (align == QStringLiteral("center")) qFlag |= Qt::AlignHCenter;
    else if (align == QStringLiteral("right"))  qFlag |= Qt::AlignRight;
    else                                        qFlag |= Qt::AlignLeft;

    p->drawText(QRect(x, y, w, h), qFlag, text);
    p->restore();
    return true;
}

}  // namespace WasabiQt::TextPainter
