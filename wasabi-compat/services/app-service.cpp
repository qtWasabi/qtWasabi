// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// app-service.cpp — Wasabi WASABI_API_APP backing.
//
// Routes the path/DPI/version queries onto Qt's standard
// cross-platform abstractions.  Strings returned to plugin code
// point at process-lifetime static storage owned by this service;
// the receiving code copies them or uses them transiently — same
// pattern Wasabi's real backing uses, just with Qt sources.
//

#include "services/app-service.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QStandardPaths>
#include <QString>

#include <mutex>
#include <vector>

namespace qtWasabi {
namespace wasabi_compat {
namespace svc {

namespace {

// Path queries return pointers that the receiving Wasabi code
// expects to remain valid for the lifetime of the service.  We
// stash each resolved path in a process-lifetime cache so the
// returned wchar_t* stays good across re-queries.
struct PathCache {
    std::mutex                       mu;
    std::vector<std::wstring>        stored;

    const wchar_t *intern(const QString &s) {
        std::lock_guard<std::mutex> lk(mu);
        stored.emplace_back(s.toStdWString());
        return stored.back().c_str();
    }
};

PathCache &pathCache() {
    static PathCache pc;
    return pc;
}

}  // anonymous

const wchar_t *AppService::getPath(int which) {
    QString q;
    switch (which) {
        case 0: q = QCoreApplication::applicationDirPath(); break;
        case 1: q = QCoreApplication::applicationDirPath(); break;
        case 2:
            q = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            break;
        default:
            q = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            break;
    }
    return pathCache().intern(q);
}

double AppService::getDpiScale() {
    if (auto *gui = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        if (auto *s = gui->primaryScreen()) {
            // Logical DPI / 96 is the canonical Win32 scale factor.
            return s->logicalDotsPerInch() / 96.0;
        }
    }
    return 1.0;
}

const wchar_t *AppService::getVersionString() {
    static const wchar_t kVersion[] = L"qtWasabi 0.1";
    return kVersion;
}

int AppService::mainCancelShutdown() {
    return 0;
}

namespace {
struct AutoRegisterApp {
    AutoRegisterApp() { registerService(&AppService::instance()); }
};
static AutoRegisterApp s_register;
}  // anonymous

}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
