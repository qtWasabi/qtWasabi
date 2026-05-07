// Stub overlay: upstream has #error port me.  Linux build never
// dispatches into the timer API from the VM layer.
#pragma once
#define _TIMERCLIENT_H 1
#define _TIMERCLIENTI_H 1

class TimerClient {
public:
    virtual ~TimerClient() = default;
    virtual void timerCallback(intptr_t id) {}
};
