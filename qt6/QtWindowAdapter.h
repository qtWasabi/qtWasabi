// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once

//
// QtWindowAdapter — bridges Wasabi's ifc_window concept onto QWidget.
//
// Wasabi's BaseWnd / VirtualWnd manage a tree of "windows" with
// HWND-style behaviour (clientToScreen, getPosition, paint, mouse
// events, sysregion masks).  This adapter wraps a QWidget so Wasabi
// code unchanged sees the familiar interface, while the actual
// rendering and event delivery happen through Qt.
//

#include "Win32Shim.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
class QPaintEvent;
class QMouseEvent;
class QKeyEvent;
class QResizeEvent;
QT_END_NAMESPACE

namespace WasabiQt {

class QtWindowAdapter : public QWidget {
public:
    explicit QtWindowAdapter(QWidget *parent = nullptr);
    ~QtWindowAdapter() override;

    // Resolve our QWidget*  ↔ HWND opaque handle — used by the
    // Wasabi-side code that internally still passes HWNDs around.
    HWND     hwnd() const noexcept;
    static QtWindowAdapter *fromHwnd(HWND h) noexcept;

    // Win32-style coord helpers Wasabi calls into.
    void clientToScreen(int *x, int *y) const;
    void screenToClient(int *x, int *y) const;
    void getClientRect(RECT *r) const;
    void getWindowRect(RECT *r) const;

    // Sysregion mask: Wasabi computes a QRegion-equivalent from
    // sysregion=-1/-2 layers; the adapter applies it via setMask().
    void setSkinRegion(const QRegion &region);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
};

} // namespace WasabiQt
