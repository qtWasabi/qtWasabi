// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once
//
// threadpool-service.h — AGAVE_API_THREADPOOL backed by QThreadPool.
//
// gen_ml + ml_* queue async work here for metadata decode, library
// scan, and image fetches.  Real Wasabi exposes a Run / RunOnGui /
// SubmitWithCallback surface; we map each onto QThreadPool::start
// + QMetaObject::invokeMethod for the GUI-thread callback.
//
// Tasks are owned by the service for their duration — we wrap
// the caller's function pointer in a small QRunnable subclass.
//

#include "service-registry.h"

#include <functional>

namespace qtWasabi {
namespace wasabi_compat {
namespace svc {

class ThreadpoolService : public ServiceObject {
public:
    GUID         guid()        const override { return THREADPOOL_GUID; }
    const char  *typeName()    const override { return "threadpool"; }
    const char  *displayName() const override { return "qtWasabi ThreadPool"; }

    // Submit `work` to the global QThreadPool.  No callback —
    // fire-and-forget.
    void run(std::function<void()> work);

    // Submit `work`; once it completes (on the worker thread),
    // marshal `done` onto the GUI thread via
    // QMetaObject::invokeMethod.  Mirrors Wasabi's
    // `SubmitWithCallback` semantics.
    void runWithCallback(std::function<void()> work,
                          std::function<void()> done);

    // Number of worker threads available.
    int  workerCount() const;

    static ThreadpoolService &instance() {
        static ThreadpoolService s;
        return s;
    }
};

}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
