// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once
//
// lang-service.h — WASABI_API_LNG passthrough.
//
// gen_ml + ml_* call `WASABI_API_LNG->StartLanguageSupport(plugin,
// guid, hinstance)` at init to associate themselves with a language
// resource bundle, then resolve UI strings through
// `WASABI_API_LNG_RetrieveString(table_id, string_id)`.
//
// The Wasabi API contract is to load a `.lng` resource DLL matching
// the active UI language and return translations.  This compat
// implementation is the identity transform: return the original
// English string, ignore the table id.  Plugins boot fine, every
// UI label is just the built-in default.  Real localisation is
// optional and not implemented here.
//

#include "service-registry.h"

namespace qtWasabi {
namespace wasabi_compat {
namespace svc {

class LangService : public ServiceObject {
public:
    GUID         guid()        const override { return LNG_GUID; }
    const char  *typeName()    const override { return "lang"; }
    const char  *displayName() const override { return "qtWasabi Language (identity)"; }

    // The Wasabi entry points gen_ml uses, exposed directly on
    // the service.  The Dispatchable bridge routes through these
    // via the api_language vtable.

    // Plugin-level localisation registration.  The Wasabi API
    // contract looks up a .lng resource bundle keyed on `pluginGuid`
    // and attaches it to the plugin's HINSTANCE for retrieve calls;
    // we return success without doing anything (identity).
    int  startLanguageSupport(void * /*plugin*/,
                                GUID    /*pluginGuid*/,
                                void * /*hinstance*/) {
        return 1;
    }

    // Retrieve a localised string.  `tableId` selects the
    // sub-bundle, `stringId` the entry within it.  Identity
    // implementation returns the default string the caller
    // supplies as fallback.
    const wchar_t *retrieveString(unsigned /*tableId*/,
                                    unsigned /*stringId*/,
                                    const wchar_t *fallback) {
        return fallback ? fallback : L"";
    }

    // Singleton accessor.  Used by the Dispatchable bridge + the
    // smoke test directly without going through GUID lookup.
    static LangService &instance() {
        static LangService s;
        return s;
    }
};

}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
