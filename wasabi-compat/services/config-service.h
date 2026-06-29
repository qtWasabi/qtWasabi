// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once
//
// config-service.h — AGAVE_API_CONFIG backed by QSettings.
//
// gen_ml stores its window-position, tree-expansion state,
// divider position, and various user prefs here.  Real Wasabi
// writes a config DB; we write a QSettings store (INI on Linux,
// plist on macOS) keyed under "qtWasabi/wasabi-compat".
//
// The Wasabi interface exposes GetInt / SetInt / GetString /
// SetString primitives keyed on (section, item).  Each (section,
// item) maps to a QSettings group/key pair so a stored value at
// "ml_local/ScannerInterval" lands at the same logical path Wasabi
// would have written it to.
//

#include "service-registry.h"

#include <QString>

namespace qtWasabi {
namespace wasabi_compat {
namespace svc {

class ConfigService : public ServiceObject {
public:
    GUID         guid()        const override { return CONFIG_GUID; }
    const char  *typeName()    const override { return "config"; }
    const char  *displayName() const override { return "qtWasabi Config (QSettings)"; }

    int     getInt   (const QString &section, const QString &key, int defaultValue) const;
    void    setInt   (const QString &section, const QString &key, int value);
    QString getString(const QString &section, const QString &key, const QString &defaultValue) const;
    void    setString(const QString &section, const QString &key, const QString &value);

    // Convenience overloads matching the canonical Wasabi
    // signatures that take wide-string keys + raw ints.
    int     getInt   (const wchar_t *section, const wchar_t *key, int defaultValue) const;
    void    setInt   (const wchar_t *section, const wchar_t *key, int value);
    const wchar_t *getString(const wchar_t *section, const wchar_t *key,
                              const wchar_t *defaultValue);
    void    setString(const wchar_t *section, const wchar_t *key,
                       const wchar_t *value);

    static ConfigService &instance() {
        static ConfigService s;
        return s;
    }
};

}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
