// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Menu.h"

#include <WasabiQt/PaintCtx.h>

namespace WasabiQt {

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

void MenuWidget::onLeftButtonDown(QPoint, PaintCtx &) {
    // Real Wasabi spawns the popup menu on press and keeps the
    // `down` button visible until the popup closes — closing via
    // click-outside or item selection.  Until we wire a real QMenu
    // popup, treat each click as a toggle so the down-state is
    // visibly sticky and a second click restores the normal state.
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

}  // namespace WasabiQt
