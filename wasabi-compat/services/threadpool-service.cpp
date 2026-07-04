// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "services/threadpool-service.h"

#include <QCoreApplication>
#include <QMetaObject>
#if !defined(__EMSCRIPTEN__)
#include <QRunnable>
#include <QThreadPool>
#endif

#include <utility>

namespace qtWasabi {
namespace wasabi_compat {
namespace svc {

namespace {

#if !defined(__EMSCRIPTEN__)
class FnRunnable : public QRunnable {
public:
    explicit FnRunnable(std::function<void()> fn)
        : m_fn(std::move(fn)) {
        setAutoDelete(true);
    }
    void run() override { if (m_fn) m_fn(); }
private:
    std::function<void()> m_fn;
};
#endif

}  // anonymous

#if defined(__EMSCRIPTEN__)
// Qt for WebAssembly (single-threaded) ships no QThreadPool: the whole
// program is one thread, so the pool degenerates to inline execution.
// Callbacks still complete in order, which is all the Wasabi threadpool
// contract promises the plugins.
void ThreadpoolService::run(std::function<void()> work) {
    if (work) work();
}

void ThreadpoolService::runWithCallback(std::function<void()> work,
                                          std::function<void()> done) {
    if (work) work();
    if (done) done();
}

int ThreadpoolService::workerCount() const { return 1; }
#else
void ThreadpoolService::run(std::function<void()> work) {
    if (!work) return;
    QThreadPool::globalInstance()->start(new FnRunnable(std::move(work)));
}

void ThreadpoolService::runWithCallback(std::function<void()> work,
                                          std::function<void()> done) {
    // Combine the two functions into a single worker that, after
    // running `work`, marshals `done` back onto the GUI thread via
    // QCoreApplication's main-thread context.  If no GUI thread
    // (qApp null), fall through to a direct call.
    auto combined = [work = std::move(work), done = std::move(done)]() {
        if (work) work();
        if (!done) return;
        if (QCoreApplication *app = QCoreApplication::instance()) {
            QMetaObject::invokeMethod(app, done, Qt::QueuedConnection);
        } else {
            done();
        }
    };
    QThreadPool::globalInstance()->start(new FnRunnable(std::move(combined)));
}

int ThreadpoolService::workerCount() const {
    return QThreadPool::globalInstance()->maxThreadCount();
}
#endif

namespace {
struct AutoRegisterThreadpool {
    AutoRegisterThreadpool() { registerService(&ThreadpoolService::instance()); }
};
static AutoRegisterThreadpool s_register;
}  // anonymous

}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
