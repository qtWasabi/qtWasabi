// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// WindowHolderRegistry — global GUID→HolderFactory map for the
// pluggable windowholder dispatch.  Thread-safe (the registry
// itself is queried from paint, which always runs on the GUI
// thread, but registrations can happen from any thread at static-
// init time so we lock anyway).
//
// GUID normalisation: `eqi`-equivalent — lowercased, `guid:`
// prefix stripped if present.  This way `guid:{6B0EDF80…}` and
// `{6B0EDF80…}` and `{6b0edf80…}` all key the same entry.
//

#include <qtWasabi/WindowHolderRegistry.h>

#include <QHash>
#include <QMutex>
#include <QMutexLocker>

namespace qtWasabi {

namespace {

QString normaliseGuid(const QString &raw) {
    QString s = raw.trimmed().toLower();
    if (s.startsWith(QLatin1String("guid:"))) s.remove(0, 5);
    return s;
}

struct Registry {
    QMutex                          mu;
    QHash<QString, HolderFactory>   factories;
    HolderFrameProvider             frameProvider;
};

Registry &registry() {
    static Registry r;
    return r;
}

}  // anonymous

void registerHolderRenderer(const QString &guid, HolderFactory factory) {
    Registry &r = registry();
    QMutexLocker lk(&r.mu);
    r.factories.insert(normaliseGuid(guid), std::move(factory));
}

void unregisterHolderRenderer(const QString &guid) {
    Registry &r = registry();
    QMutexLocker lk(&r.mu);
    r.factories.remove(normaliseGuid(guid));
}

const HolderFactory *lookupHolderRenderer(const QString &guid) {
    Registry &r = registry();
    QMutexLocker lk(&r.mu);
    auto it = r.factories.constFind(normaliseGuid(guid));
    if (it == r.factories.constEnd()) return nullptr;
    return &it.value();
}

void registerHolderFrameProvider(HolderFrameProvider provider) {
    Registry &r = registry();
    QMutexLocker lk(&r.mu);
    r.frameProvider = std::move(provider);
}

QImage holderFrameFor(const QString &guidKey, const QSize &size) {
    // Copy the std::function out under the lock, then invoke it
    // unlocked — the provider reads from the embedder (a mutex-guarded
    // MilkdropItem frame) and must not run while holding the registry
    // lock, which paint() may re-enter via lookupHolderRenderer.
    HolderFrameProvider fn;
    {
        Registry &r = registry();
        QMutexLocker lk(&r.mu);
        fn = r.frameProvider;
    }
    if (!fn) return QImage();
    return fn(guidKey, size);
}

}  // namespace qtWasabi
