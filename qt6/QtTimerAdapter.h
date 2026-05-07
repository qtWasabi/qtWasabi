// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once

//
// QtTimerAdapter — wraps QTimer to satisfy the Wasabi Timer object
// surface that Maki scripts use (setDelay, start, stop, onTimer).
//

#include <QObject>
#include <QTimer>
#include <functional>

namespace WasabiQt {

class QtTimerAdapter : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void()>;

    explicit QtTimerAdapter(QObject *parent = nullptr);

    void setDelay(int ms);
    void start();
    void stop();
    bool isActive() const;
    void setCallback(Callback cb);

private:
    QTimer m_timer;
    Callback m_cb;
};

} // namespace WasabiQt
