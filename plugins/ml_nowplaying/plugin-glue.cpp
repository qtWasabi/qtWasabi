// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// plugin-glue.cpp — service-registry plumbing.
//
// Builds a minimum-viable Wasabi service substrate so the plugin's
// `Wasabi_QueryInterface(GUID)` chain works end-to-end:
//
//     1. Plugin asks `plugin.service->service_getServiceByGuid(GUID)`.
//     2. Our `RegistryApiService` looks the GUID up in a static map
//        and returns the matching `waServiceFactory*`, or NULL.
//     3. Plugin calls `factory->getInterface()` on the result.
//     4. Our `StubFactory` returns the attached interface pointer.
//
// Each registered "interface" is a `NullDispatchable` whose
// `_dispatch` always returns 0 — meaning every method on the
// interface falls through to its `defval` argument (NULL / 0).
// Plugin code's existing null/zero checks handle that cleanly.
//
// Registered services:
//   - OBJ_OmBrowser              — gates ml_nowplaying::Plugin_Init
//     ({D5325EAB-9BD7-4382-A31D-38EF603061B3})
//   - languageApiGUID            — resource-string lookups for the
//     "Now Playing" tree-item name (IDS_SERVICE_NAME = 1).
//     ({30AED4E5-EF10-4277-8D49-27AB5570E891})
//
// Other Wasabi APIs (api_application, api_syscb, JSAPI2::api_security,
// ifc_omutility) are deliberately left unregistered: the plugin
// null-checks their results and skips the relevant init steps
// when missing.
//

#include <bfc/dispatch.h>
#include <api/service/api_service.h>
#include <api/service/waservicefactory.h>

#include <cstdio>
#include <cstring>  // memcmp for GUID compare

namespace {

// ── Universal null Dispatchable ─────────────────────────────────
// Every method on an interface inheriting from this routes via
// `_call(MSG_ID, defval, args...)`.  `_call` invokes `_dispatch`;
// returning 0 means "not handled", and `_call` returns its
// `defval` argument — typically NULL/0/empty.  This is the
// "behaviourally inert but linkage-valid" stub object.
class NullDispatchable : public Dispatchable {
public:
    int _dispatch(int /*msg*/, void * /*retval*/,
                   void ** /*params*/, int /*nparam*/) override {
        return 0;
    }
};

// ── api_language stub ───────────────────────────────────────────
// Handles `GetStringW(hinst, owner, uID, buf, maxlen)` to return a
// hardcoded string for ml_nowplaying's known resource IDs.  Other
// methods fall through to NULL defaults.  Without this, the
// plugin's `Plugin_CopyResString` resolves a NULL string and
// returns E_FAIL — blocking tree-item insertion.
class LanguageApiStub : public Dispatchable {
public:
    int _dispatch(int msg, void *retval,
                   void **params, int nparam) override {
        if (msg == 11 /* API_LANGUAGE_GETSTRINGW */ && nparam >= 5) {
            // Param order: hinst, owner, uID, str, maxlen.
            // _call wraps each by address; so params[i] is &arg_i.
            UINT uID = *reinterpret_cast<UINT *>(params[2]);
            wchar_t *buf = *reinterpret_cast<wchar_t **>(params[3]);
            size_t maxlen = *reinterpret_cast<size_t *>(params[4]);

            const wchar_t *src = L"";
            switch (uID) {
                case 1:      // IDS_SERVICE_NAME
                    src = L"Now Playing";
                    break;
                case 65534:  // IDS_PLUGIN_NAME
                    src = L"Now Playing";
                    break;
                default:
                    src = L"";
                    break;
            }

            if (buf && maxlen > 0) {
                size_t i = 0;
                while (i + 1 < maxlen && src[i] != 0) {
                    buf[i] = src[i];
                    ++i;
                }
                buf[i] = 0;
            }

            if (retval) {
                *reinterpret_cast<const wchar_t **>(retval) =
                    buf ? buf : src;
            }
            return 1;
        }
        return 0;
    }
};

// ── Stub waServiceFactory ───────────────────────────────────────
// Holds the GUID it represents + the interface pointer to hand
// back from `getInterface()`.  Dispatches the GETINTERFACE, GETGUID,
// and RELEASEINTERFACE messages; leaves everything else to fall
// through.
class StubFactory : public waServiceFactory {
public:
    StubFactory(GUID g, void *iface) : m_guid(g), m_iface(iface) {}

    int _dispatch(int msg, void *retval,
                   void **params, int nparam) override {
        switch (msg) {
            // getInterface(int global_lock)
            case 300:  // WASERVICEFACTORY_GETINTERFACE
                if (retval) {
                    *reinterpret_cast<void **>(retval) = m_iface;
                }
                (void)params; (void)nparam;
                return 1;
            // getGuid()
            case 210:  // WASERVICEFACTORY_GETGUID
                if (retval) {
                    *reinterpret_cast<GUID *>(retval) = m_guid;
                }
                return 1;
            // releaseInterface(void *ifc) — accept, do nothing
            case 310:  // WASERVICEFACTORY_RELEASEINTERFACE
                if (retval) {
                    *reinterpret_cast<int *>(retval) = 0;
                }
                return 1;
            default:
                return 0;
        }
    }

private:
    GUID  m_guid;
    void *m_iface;
};

// ── Registry api_service ────────────────────────────────────────
// Holds a fixed list of (GUID → factory) entries.  Dispatches
// API_SERVICE_SERVICE_GETSERVICEBYGUID by walking the list.
struct RegistryEntry {
    GUID                  guid;
    waServiceFactory     *factory;
};

class RegistryApiService : public api_service {
public:
    RegistryApiService(const RegistryEntry *entries, size_t count)
        : m_entries(entries), m_count(count) {}

    int _dispatch(int msg, void *retval,
                   void **params, int nparam) override {
        switch (msg) {
            // service_getServiceByGuid(GUID guid)
            case 50:  // API_SERVICE_SERVICE_GETSERVICEBYGUID
                if (retval && params && nparam >= 1) {
                    // params[0] points to the GUID argument the
                    // caller passed by value.
                    const GUID *queryGuid =
                        reinterpret_cast<const GUID *>(params[0]);
                    waServiceFactory *match = nullptr;
                    if (queryGuid) {
                        for (size_t i = 0; i < m_count; ++i) {
                            if (memcmp(&m_entries[i].guid, queryGuid,
                                       sizeof(GUID)) == 0) {
                                match = m_entries[i].factory;
                                break;
                            }
                        }
                    }
                    *reinterpret_cast<waServiceFactory **>(retval) = match;
                    return 1;
                }
                return 0;
            // service_release / service_lock / service_unlock —
            // accept silently so plugin's lock/unlock pairs balance
            // without errors.
            case 60:   // API_SERVICE_SERVICE_LOCK
            case 70:   // API_SERVICE_SERVICE_CLIENTLOCK
            case 80:   // API_SERVICE_SERVICE_RELEASE
            case 120:  // API_SERVICE_SERVICE_UNLOCK
                if (retval) {
                    *reinterpret_cast<int *>(retval) = 0;
                }
                return 1;
            default:
                return 0;
        }
    }

private:
    const RegistryEntry *m_entries;
    size_t               m_count;
};

// ── Process-singleton service tree ──────────────────────────────
// Construct the stub interface objects, the factories that wrap
// them, and the api_service that hosts the factories.  All have
// static-storage lifetime so they live the whole process and the
// pointers we hand out stay valid forever.

NullDispatchable &nullOmBrowser() {
    static NullDispatchable inst;
    return inst;
}

LanguageApiStub &languageApi() {
    static LanguageApiStub inst;
    return inst;
}

StubFactory &omBrowserFactory() {
    // OBJ_OmBrowser = {D5325EAB-9BD7-4382-A31D-38EF603061B3}
    static const GUID kObjOmBrowser = {
        0xd5325eab, 0x9bd7, 0x4382,
        {0xa3, 0x1d, 0x38, 0xef, 0x60, 0x30, 0x61, 0xb3}
    };
    static StubFactory inst(kObjOmBrowser, &nullOmBrowser());
    return inst;
}

StubFactory &languageFactory() {
    // languageApiGUID = {30AED4E5-EF10-4277-8D49-27AB5570E891}
    static const GUID kLanguageApi = {
        0x30aed4e5, 0xef10, 0x4277,
        {0x8d, 0x49, 0x27, 0xab, 0x55, 0x70, 0xe8, 0x91}
    };
    static StubFactory inst(kLanguageApi, &languageApi());
    return inst;
}

RegistryApiService &registryApiService() {
    static const RegistryEntry kEntries[] = {
        {  // OBJ_OmBrowser
            {0xd5325eab, 0x9bd7, 0x4382,
             {0xa3, 0x1d, 0x38, 0xef, 0x60, 0x30, 0x61, 0xb3}},
            &omBrowserFactory()
        },
        {  // languageApiGUID
            {0x30aed4e5, 0xef10, 0x4277,
             {0x8d, 0x49, 0x27, 0xab, 0x55, 0x70, 0xe8, 0x91}},
            &languageFactory()
        },
    };
    static RegistryApiService inst(
        kEntries, sizeof(kEntries) / sizeof(kEntries[0]));
    return inst;
}

}  // anonymous

extern "C" api_service *qtwasabi_null_api_service() {
    return &registryApiService();
}
