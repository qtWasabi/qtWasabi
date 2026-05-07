// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once

//
// QtCanvasAdapter — bridges Wasabi's GDI-style canvas API onto QPainter.
//
// Wasabi's BltCanvas / Canvas / TextInfoCanvas (api/wnd/*) use Win32
// HDC concepts: BitBlt, StretchBlt, TextOut, GetTextExtentPoint32,
// CreateFont, etc.  This adapter routes those calls through Qt's
// QPainter, with HiDPI awareness and Wayland-safe transforms.
//
// Reference (NOT source): /Src/Wasabi/qt6/QtCanvasAdapter.{h,cpp} —
// the 2015-era stub.  Useful for what surface to expose; the
// implementation here is fresh.
//

#include "Win32Shim.h"
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <memory>

QT_BEGIN_NAMESPACE
class QPaintDevice;
QT_END_NAMESPACE

namespace WasabiQt {

class QtCanvasAdapter {
public:
    // Construct around an existing QPainter (typical: from
    // QWidget::paintEvent).  Adapter does not own the painter.
    explicit QtCanvasAdapter(QPainter *painter);

    // Construct around a QImage owned by the adapter (offscreen
    // canvas, used by Wasabi for sub-bitmap composition).
    explicit QtCanvasAdapter(int w, int h);

    ~QtCanvasAdapter();

    // ── Win32 GDI surface used by Wasabi ───────────────────────
    // Each method below maps to a Wasabi Canvas member that
    // internally calls into GDI on Windows.

    void blit(int sx, int sy, int sw, int sh,
              QtCanvasAdapter *dst, int dx, int dy);
    void stretchBlit(int sx, int sy, int sw, int sh,
                     QtCanvasAdapter *dst, int dx, int dy, int dw, int dh);
    void fillRect(int x, int y, int w, int h, COLORREF colour);
    void textOut(int x, int y, const QString &text);
    int  getTextWidth(const QString &text) const;
    int  getTextHeight(const QString &text) const;
    void setFont(const QString &face, int pixelSize, bool bold,
                 bool italic, bool underline);
    void setTextColor(COLORREF colour);
    void setBkColor(COLORREF colour);

    // QImage access — used by region-mask building, sysregion-2
    // alpha extraction, etc.
    QImage  &image();
    const QImage &image() const;

    QPainter *painter() { return m_painter.get() ? m_painter.get() : m_borrowed; }

private:
    QImage   m_image;
    std::unique_ptr<QPainter> m_painter;   // when we own the device
    QPainter *m_borrowed = nullptr;        // when handed an external painter
};

} // namespace WasabiQt
