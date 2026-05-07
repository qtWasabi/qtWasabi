// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "QtWindowAdapter.h"

#include <QHash>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QKeyEvent>

namespace WasabiQt {

namespace {
// Cookie ↔ adapter registry.  HWND opaques are non-null pointers we
// hand to Wasabi; we resolve them back here.  Generation counter so
// stale HWNDs from destroyed adapters don't accidentally match.
QHash<intptr_t, QtWindowAdapter*> &registry() {
    static QHash<intptr_t, QtWindowAdapter*> r;
    return r;
}
intptr_t nextId() {
    static intptr_t n = 1;
    return n++;
}
}

QtWindowAdapter::QtWindowAdapter(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);
    registry().insert(reinterpret_cast<intptr_t>(this), this);
}

QtWindowAdapter::~QtWindowAdapter()
{
    registry().remove(reinterpret_cast<intptr_t>(this));
}

HWND QtWindowAdapter::hwnd() const noexcept
{
    return reinterpret_cast<HWND>(const_cast<QtWindowAdapter*>(this));
}

QtWindowAdapter *QtWindowAdapter::fromHwnd(HWND h) noexcept
{
    return registry().value(reinterpret_cast<intptr_t>(h), nullptr);
}

void QtWindowAdapter::clientToScreen(int *x, int *y) const
{
    QPoint p(x ? *x : 0, y ? *y : 0);
    p = mapToGlobal(p);
    if (x) *x = p.x();
    if (y) *y = p.y();
}

void QtWindowAdapter::screenToClient(int *x, int *y) const
{
    QPoint p(x ? *x : 0, y ? *y : 0);
    p = mapFromGlobal(p);
    if (x) *x = p.x();
    if (y) *y = p.y();
}

void QtWindowAdapter::getClientRect(RECT *r) const
{
    if (!r) return;
    r->left = 0; r->top = 0;
    r->right  = width();
    r->bottom = height();
}

void QtWindowAdapter::getWindowRect(RECT *r) const
{
    if (!r) return;
    QPoint tl = mapToGlobal(QPoint(0, 0));
    r->left   = tl.x();
    r->top    = tl.y();
    r->right  = tl.x() + width();
    r->bottom = tl.y() + height();
}

void QtWindowAdapter::setSkinRegion(const QRegion &region)
{
    setMask(region);
}

// Stub paint — real paint will route through Wasabi's widget tree
// once skin loading is wired up in src/.
void QtWindowAdapter::paintEvent(QPaintEvent *event) { Q_UNUSED(event); }

void QtWindowAdapter::mousePressEvent(QMouseEvent *event)   { Q_UNUSED(event); }
void QtWindowAdapter::mouseReleaseEvent(QMouseEvent *event) { Q_UNUSED(event); }
void QtWindowAdapter::mouseMoveEvent(QMouseEvent *event)    { Q_UNUSED(event); }
void QtWindowAdapter::keyPressEvent(QKeyEvent *event)       { Q_UNUSED(event); }
void QtWindowAdapter::resizeEvent(QResizeEvent *event)      { Q_UNUSED(event); }

} // namespace WasabiQt
