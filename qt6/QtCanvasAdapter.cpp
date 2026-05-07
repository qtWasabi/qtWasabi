// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "QtCanvasAdapter.h"

#include <QFont>
#include <QFontMetrics>

namespace WasabiQt {

QtCanvasAdapter::QtCanvasAdapter(QPainter *painter)
    : m_borrowed(painter)
{}

QtCanvasAdapter::QtCanvasAdapter(int w, int h)
    : m_image(w, h, QImage::Format_ARGB32_Premultiplied)
{
    m_image.fill(Qt::transparent);
    m_painter = std::make_unique<QPainter>(&m_image);
    m_painter->setRenderHint(QPainter::Antialiasing, true);
    m_painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
}

QtCanvasAdapter::~QtCanvasAdapter() = default;

void QtCanvasAdapter::blit(int sx, int sy, int sw, int sh,
                            QtCanvasAdapter *dst, int dx, int dy)
{
    if (!dst || !dst->painter()) return;
    dst->painter()->drawImage(QPoint(dx, dy), m_image,
                              QRect(sx, sy, sw, sh));
}

void QtCanvasAdapter::stretchBlit(int sx, int sy, int sw, int sh,
                                   QtCanvasAdapter *dst,
                                   int dx, int dy, int dw, int dh)
{
    if (!dst || !dst->painter()) return;
    dst->painter()->drawImage(QRect(dx, dy, dw, dh), m_image,
                              QRect(sx, sy, sw, sh));
}

void QtCanvasAdapter::fillRect(int x, int y, int w, int h, COLORREF c)
{
    if (auto *p = painter()) {
        // Win32 COLORREF is 0x00BBGGRR; QColor expects RGB.
        QColor qc(int(c & 0xFF),
                  int((c >> 8) & 0xFF),
                  int((c >> 16) & 0xFF));
        p->fillRect(x, y, w, h, qc);
    }
}

void QtCanvasAdapter::textOut(int x, int y, const QString &text)
{
    if (auto *p = painter()) {
        // Wasabi positions text by baseline-style coords; Qt's
        // drawText(x,y,...) takes baseline.
        p->drawText(x, y + p->fontMetrics().ascent(), text);
    }
}

int QtCanvasAdapter::getTextWidth(const QString &text) const
{
    auto *p = const_cast<QtCanvasAdapter*>(this)->painter();
    if (!p) return 0;
    return p->fontMetrics().horizontalAdvance(text);
}

int QtCanvasAdapter::getTextHeight(const QString &text) const
{
    Q_UNUSED(text);
    auto *p = const_cast<QtCanvasAdapter*>(this)->painter();
    if (!p) return 0;
    return p->fontMetrics().height();
}

void QtCanvasAdapter::setFont(const QString &face, int pixelSize, bool bold,
                               bool italic, bool underline)
{
    if (auto *p = painter()) {
        QFont f(face);
        if (pixelSize > 0) f.setPixelSize(pixelSize);
        f.setBold(bold);
        f.setItalic(italic);
        f.setUnderline(underline);
        p->setFont(f);
    }
}

void QtCanvasAdapter::setTextColor(COLORREF c)
{
    if (auto *p = painter()) {
        QColor qc(int(c & 0xFF),
                  int((c >> 8) & 0xFF),
                  int((c >> 16) & 0xFF));
        p->setPen(qc);
    }
}

void QtCanvasAdapter::setBkColor(COLORREF c)
{
    if (auto *p = painter()) {
        QColor qc(int(c & 0xFF),
                  int((c >> 8) & 0xFF),
                  int((c >> 16) & 0xFF));
        p->setBackground(qc);
    }
}

QImage       &QtCanvasAdapter::image()       { return m_image; }
const QImage &QtCanvasAdapter::image() const { return m_image; }

} // namespace WasabiQt
