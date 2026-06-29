// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// service-registry.cpp — thread-safe service registry
// implementation.  Linear-scan lookup is fine for the small set
// of services Wasabi defines (~12 of interest); GUID comparison
// is cheap (16 bytes).
//

#include "services/service-registry.h"

#include <mutex>
#include <vector>

namespace qtWasabi {
namespace wasabi_compat {
namespace svc {

namespace {

struct Registry {
    std::mutex                  mu;
    std::vector<ServiceObject *> services;
};

Registry &registry() {
    static Registry r;
    return r;
}

}  // anonymous

void registerService(ServiceObject *svc) {
    if (!svc) return;
    auto &r = registry();
    std::lock_guard<std::mutex> lk(r.mu);
    // Replace any existing entry with the same GUID — Wasabi's
    // registry semantics allow re-registration (used during
    // service-restart scenarios).
    const GUID g = svc->guid();
    for (auto &s : r.services) {
        if (s && guidEq(s->guid(), g)) {
            s = svc;
            return;
        }
    }
    r.services.push_back(svc);
}

ServiceObject *lookupService(GUID g) {
    auto &r = registry();
    std::lock_guard<std::mutex> lk(r.mu);
    for (auto *s : r.services) {
        if (s && guidEq(s->guid(), g)) return s;
    }
    return nullptr;
}

void unregisterService(ServiceObject *svc) {
    if (!svc) return;
    auto &r = registry();
    std::lock_guard<std::mutex> lk(r.mu);
    for (auto it = r.services.begin(); it != r.services.end(); ++it) {
        if (*it == svc) { r.services.erase(it); return; }
    }
}

std::size_t serviceCount() {
    auto &r = registry();
    std::lock_guard<std::mutex> lk(r.mu);
    return r.services.size();
}

ServiceObject *serviceAt(std::size_t index) {
    auto &r = registry();
    std::lock_guard<std::mutex> lk(r.mu);
    return index < r.services.size() ? r.services[index] : nullptr;
}

}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
