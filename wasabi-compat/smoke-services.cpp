// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// smoke-services.cpp — smoke test for the service
// registry.  Registers a synthetic service against a known GUID,
// verifies lookup-by-guid + iteration + replace-on-reregister.
//

#include "services/service-registry.h"

#include "smoke-check.h"

namespace qtWasabi {
namespace wasabi_compat {
namespace svc {
namespace {

// Synthetic GUID disjoint from the real WASABI_API_* GUIDs so
// the smoke doesn't collide with services other TUs register at
// static-init.  Distinct enough to be unmistakable in any leak
// trace (`FAKE-…`).
inline constexpr GUID FAKE_GUID =
    makeGuid(0xFAFEFAFE, 0x1234, 0x5678,
              0x9A, 0xBC, 0xDE, 0xF0, 0xFE, 0xED, 0xBE, 0xEF);

class FakeService : public ServiceObject {
public:
    explicit FakeService(int tag) : m_tag(tag) {}
    GUID guid() const override { return FAKE_GUID; }
    const char *displayName() const override { return "fake smoke service"; }
    int tag() const { return m_tag; }
private:
    int m_tag;
};

struct ServicesSmoke {
    ServicesSmoke() {
        // GUID equality round-trip.
        static_assert(guidEq(LNG_GUID, LNG_GUID),
                      "LNG_GUID is equal to itself");
        static_assert(!guidEq(LNG_GUID, APP_GUID),
                      "LNG_GUID is distinct from APP_GUID");
        static_assert(!guidEq(FAKE_GUID, LNG_GUID),
                      "FAKE_GUID is distinct from any real service");

        // Empty-slot lookup returns null.  Other TUs may register
        // their own services at static-init; we only assert that
        // OUR fake GUID is initially absent.
        SMOKE_CHECK(lookupService(FAKE_GUID) == nullptr);

        // Snapshot the registry size — other TUs (lang-service,
        // and any other services) may have already registered
        // themselves at this point.  We assert our +1 / -1
        // deltas instead of absolute counts.
        const std::size_t baseline = serviceCount();

        FakeService svc1(1);
        registerService(&svc1);
        ServiceObject *got = lookupService(FAKE_GUID);
        SMOKE_CHECK(got == &svc1);
        SMOKE_CHECK(static_cast<FakeService *>(got)->tag() == 1);
        SMOKE_CHECK(serviceCount() == baseline + 1);

        // Re-register with same GUID replaces (no duplicate).
        FakeService svc2(2);
        registerService(&svc2);
        SMOKE_CHECK(serviceCount() == baseline + 1);
        SMOKE_CHECK(lookupService(FAKE_GUID) == &svc2);

        // Unregister.
        unregisterService(&svc2);
        SMOKE_CHECK(serviceCount() == baseline);
        SMOKE_CHECK(lookupService(FAKE_GUID) == nullptr);
    }
};
static ServicesSmoke s_smoke;

}  // anonymous
}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
