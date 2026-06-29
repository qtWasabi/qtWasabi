// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Menu.h"

#include <qtWasabi/PaintCtx.h>

namespace qtWasabi {

namespace {
// Set a widget's `visible` attr and route through setXmlParam so
// any subclass-typed shadow stays in sync (and the registry's
// repaint callback fires).
void setVisible(Widget *w, bool on) {
    if (!w) return;
    w->setXmlParam(QStringLiteral("visible"),
                    on ? QStringLiteral("1") : QStringLiteral("0"));
}
}  // namespace

void MenuWidget::updateObjects() {
    Widget *normal = findById(attrs.value(QStringLiteral("normal")));
    Widget *down   = findById(attrs.value(QStringLiteral("down")));
    Widget *hover  = findById(attrs.value(QStringLiteral("hover")));
    setVisible(normal, !m_isSpawned);
    setVisible(down,    m_isSpawned);
    setVisible(hover,  !m_isSpawned && m_inArea);
}

void MenuWidget::setSpawned(bool on) {
    if (m_isSpawned == on) return;
    m_isSpawned = on;
    updateObjects();
}

void MenuWidget::onLeftButtonDown(QPoint, PaintCtx &) {
    // The embedder drives the real popup (it owns the screen geometry +
    // Host actions) and toggles setSpawned() around it.  If no embedder
    // handles this menu, fall back to a sticky toggle so the down-state
    // is still visible.
    m_isSpawned = !m_isSpawned;
    updateObjects();
}

void MenuWidget::onLeftButtonUp(QPoint, PaintCtx &) {
    // No-op until a real popup spawns — the visible state lives on
    // press only.  Once QMenu integration lands, this handler will
    // anchor + show the popup window.
}

void MenuWidget::onMouseMove(QPoint, PaintCtx &) {
    if (!m_inArea) {
        m_inArea = true;
        updateObjects();
    }
}

void MenuWidget::onMouseLeave(PaintCtx &) {
    if (m_inArea) {
        m_inArea = false;
        updateObjects();
    }
}

}  // namespace qtWasabi
