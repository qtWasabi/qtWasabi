// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "QtTimerAdapter.h"

namespace WasabiQt {

QtTimerAdapter::QtTimerAdapter(QObject *parent)
    : QObject(parent)
{
    QObject::connect(&m_timer, &QTimer::timeout, this, [this] {
        if (m_cb) m_cb();
    });
}

void QtTimerAdapter::setDelay(int ms)       { m_timer.setInterval(ms); }
void QtTimerAdapter::start()                 { m_timer.start(); }
void QtTimerAdapter::stop()                  { m_timer.stop(); }
bool QtTimerAdapter::isActive() const        { return m_timer.isActive(); }
void QtTimerAdapter::setCallback(Callback cb){ m_cb = std::move(cb); }

} // namespace WasabiQt
