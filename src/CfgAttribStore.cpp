// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <qtWasabi/CfgAttribStore.h>

namespace qtWasabi {

CfgAttribStore &CfgAttribStore::instance() {
    static CfgAttribStore s;
    return s;
}

int CfgAttribStore::get(const QString &key) const {
    return m_values.value(key, 0);
}

bool CfgAttribStore::has(const QString &key) const {
    return m_values.contains(key);
}

void CfgAttribStore::set(const QString &key, int value) {
    auto it = m_values.find(key);
    if (it != m_values.end() && it.value() == value) return;
    m_values.insert(key, value);
    // Copy subscriber callbacks before invoking — a callback may
    // unsubscribe (or subscribe) during dispatch and we don't want
    // the iterator to invalidate mid-loop.
    QList<Subscriber> toFire;
    for (auto sit = m_subs.constBegin(); sit != m_subs.constEnd(); ++sit) {
        if (sit.value().key == key) toFire.append(sit.value().cb);
    }
    for (const auto &cb : toFire) cb(value);
}

int CfgAttribStore::subscribe(const QString &key, Subscriber cb) {
    const int h = m_nextHandle++;
    m_subs.insert(h, Sub{key, std::move(cb)});
    return h;
}

void CfgAttribStore::unsubscribe(int handle) {
    m_subs.remove(handle);
}

}  // namespace qtWasabi
