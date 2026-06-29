// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "services/config-service.h"

#include <QSettings>

#include <mutex>
#include <string>
#include <vector>

namespace qtWasabi {
namespace wasabi_compat {
namespace svc {

namespace {

// Cached settings object — created once, shared across all
// calls.  QSettings on Linux is INI-backed, which is fast and
// human-debuggable.  Path: $XDG_CONFIG_HOME/qtWasabi/wasabi-compat.conf
QSettings &settings() {
    static QSettings s(QSettings::IniFormat,
                        QSettings::UserScope,
                        QStringLiteral("qtWasabi"),
                        QStringLiteral("wasabi-compat"));
    return s;
}

// String returns to wide-string callers need process-lifetime
// storage so the returned pointer stays valid.  Same pattern
// AppService uses for path queries.
struct StringCache {
    std::mutex                mu;
    std::vector<std::wstring> stored;

    const wchar_t *intern(const QString &s) {
        std::lock_guard<std::mutex> lk(mu);
        stored.emplace_back(s.toStdWString());
        return stored.back().c_str();
    }
};

StringCache &stringCache() {
    static StringCache c;
    return c;
}

QString keyPath(const QString &section, const QString &key) {
    return section + QLatin1Char('/') + key;
}

}  // anonymous

int ConfigService::getInt(const QString &section, const QString &key, int defaultValue) const {
    return settings().value(keyPath(section, key), defaultValue).toInt();
}

void ConfigService::setInt(const QString &section, const QString &key, int value) {
    settings().setValue(keyPath(section, key), value);
}

QString ConfigService::getString(const QString &section, const QString &key,
                                   const QString &defaultValue) const {
    return settings().value(keyPath(section, key), defaultValue).toString();
}

void ConfigService::setString(const QString &section, const QString &key,
                                const QString &value) {
    settings().setValue(keyPath(section, key), value);
}

int ConfigService::getInt(const wchar_t *section, const wchar_t *key, int defaultValue) const {
    return getInt(QString::fromWCharArray(section ? section : L""),
                   QString::fromWCharArray(key ? key : L""),
                   defaultValue);
}

void ConfigService::setInt(const wchar_t *section, const wchar_t *key, int value) {
    setInt(QString::fromWCharArray(section ? section : L""),
            QString::fromWCharArray(key ? key : L""),
            value);
}

const wchar_t *ConfigService::getString(const wchar_t *section, const wchar_t *key,
                                          const wchar_t *defaultValue) {
    QString v = getString(QString::fromWCharArray(section ? section : L""),
                           QString::fromWCharArray(key ? key : L""),
                           QString::fromWCharArray(defaultValue ? defaultValue : L""));
    return stringCache().intern(v);
}

void ConfigService::setString(const wchar_t *section, const wchar_t *key,
                                const wchar_t *value) {
    setString(QString::fromWCharArray(section ? section : L""),
               QString::fromWCharArray(key ? key : L""),
               QString::fromWCharArray(value ? value : L""));
}

namespace {
struct AutoRegisterConfig {
    AutoRegisterConfig() { registerService(&ConfigService::instance()); }
};
static AutoRegisterConfig s_register;
}  // anonymous

}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
