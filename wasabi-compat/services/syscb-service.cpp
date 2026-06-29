// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "services/syscb-service.h"

#include <mutex>
#include <vector>

namespace qtWasabi {
namespace wasabi_compat {
namespace svc {

namespace {

struct CbList {
    std::mutex                mu;
    std::vector<SysCallback *> list;
};

CbList &cbList() {
    static CbList c;
    return c;
}

}  // anonymous

void SysCbService::registerCallback(SysCallback *cb) {
    if (!cb) return;
    auto &c = cbList();
    std::lock_guard<std::mutex> lk(c.mu);
    c.list.push_back(cb);
}

void SysCbService::deregisterCallback(SysCallback *cb) {
    if (!cb) return;
    auto &c = cbList();
    std::lock_guard<std::mutex> lk(c.mu);
    for (auto it = c.list.begin(); it != c.list.end(); ++it) {
        if (*it == cb) { c.list.erase(it); return; }
    }
}

int SysCbService::issueEvent(uint32_t eventClass, uintptr_t p1, uintptr_t p2) {
    auto &c = cbList();
    // Snapshot under lock, dispatch outside it — Wasabi convention
    // allows listeners to deregister themselves from inside their
    // notify call.
    std::vector<SysCallback *> snapshot;
    {
        std::lock_guard<std::mutex> lk(c.mu);
        snapshot = c.list;
    }
    int acc = 0;
    for (auto *cb : snapshot) {
        if (cb && cb->getEventClass() == eventClass)
            acc |= cb->notifyEvent(eventClass, p1, p2);
    }
    return acc;
}

namespace {
struct AutoRegisterSysCb {
    AutoRegisterSysCb() { registerService(&SysCbService::instance()); }
};
static AutoRegisterSysCb s_register;
}  // anonymous

}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
