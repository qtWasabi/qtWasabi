// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// smoke-phase-b.cpp — end-to-end verification.
//
// Mirrors gen_ml's `init()` sequence of service queries.  Real
// gen_ml does:
//
//     WASABI_API_SVC = (api_service *)SendMessage(parent, WM_WA_IPC,
//                                                   0, IPC_GET_API_SERVICE);
//     ...
//     ServiceBuild(WASABI_API_LNG, languageApiGUID);
//     ServiceBuild(AGAVE_API_DECODE, decodeFileGUID);
//     ServiceBuild(WASABI_API_APP, applicationApiServiceGuid);
//     ...
//
// Each `ServiceBuild` macro expands to a call into
// `service_getServiceByGuid(GUID)`.  We call our equivalent
// `lookupService(GUID)` for each Wasabi service we expose and
// assert a non-null backing exists.  If every service is
// reachable, gen_ml's analogous init path returns
// `GEN_INIT_SUCCESS`.
//

#include "services/app-service.h"
#include "services/config-service.h"
#include "services/decode-service.h"
#include "services/lang-service.h"
#include "services/palette-service.h"
#include "services/service-registry.h"
#include "services/skin-service.h"
#include "services/syscb-service.h"
#include "services/threadpool-service.h"
#include "services/wnd-service.h"

#include "smoke-check.h"

namespace qtWasabi {
namespace wasabi_compat {
namespace svc {
namespace {

struct PhaseBSmoke {
    PhaseBSmoke() {
        // 1. Every service GUID resolves to a registered factory.
        //    Mirrors gen_ml's ServiceBuild() chain at init.
        SMOKE_CHECK(lookupService(LNG_GUID)        != nullptr);
        SMOKE_CHECK(lookupService(APP_GUID)        != nullptr);
        SMOKE_CHECK(lookupService(WND_GUID)        != nullptr);
        SMOKE_CHECK(lookupService(SKIN_GUID)       != nullptr);
        SMOKE_CHECK(lookupService(CONFIG_GUID)     != nullptr);
        SMOKE_CHECK(lookupService(THREADPOOL_GUID) != nullptr);
        SMOKE_CHECK(lookupService(DECODE_GUID)     != nullptr);
        SMOKE_CHECK(lookupService(SYSCB_GUID)      != nullptr);
        SMOKE_CHECK(lookupService(PALETTE_GUID)    != nullptr);

        // 2. Each service identity-checks against its singleton.
        //    Confirms the static-init AutoRegister handed back the
        //    right backing instance.
        SMOKE_CHECK(lookupService(LNG_GUID)        == &LangService::instance());
        SMOKE_CHECK(lookupService(APP_GUID)        == &AppService::instance());
        SMOKE_CHECK(lookupService(WND_GUID)        == &WndService::instance());
        SMOKE_CHECK(lookupService(SKIN_GUID)       == &SkinService::instance());
        SMOKE_CHECK(lookupService(CONFIG_GUID)     == &ConfigService::instance());
        SMOKE_CHECK(lookupService(THREADPOOL_GUID) == &ThreadpoolService::instance());
        SMOKE_CHECK(lookupService(DECODE_GUID)     == &DecodeService::instance());
        SMOKE_CHECK(lookupService(SYSCB_GUID)      == &SysCbService::instance());
        SMOKE_CHECK(lookupService(PALETTE_GUID)    == &PaletteService::instance());

        // 3. Round-trip one method per service so the link path
        //    is exercised (any unresolved symbol crashes here).
        //    Language: identity transform on a fallback wstring.
        const wchar_t *got = LangService::instance().retrieveString(
            0, 0, L"Local Library");
        SMOKE_CHECK(got != nullptr);

        // App: getPath should return something non-null (Qt
        // path resolution can yield empty strings on minimal CI
        // environments, so don't compare against a specific path).
        SMOKE_CHECK(AppService::instance().getPath(0) != nullptr);
        SMOKE_CHECK(AppService::instance().getVersionString() != nullptr);

        // Skin: the bound runtime starts null until the embedder
        // injects one.
        SMOKE_CHECK(!SkinService::instance().isModernSkinLoaded());

        // Config: round-trip an int.  Uses a unique section so we
        // don't collide with any real config persistence.
        ConfigService::instance().setInt("smoke-phase-b", "marker", 12345);
        SMOKE_CHECK(ConfigService::instance().getInt(
            "smoke-phase-b", "marker", 0) == 12345);

        // Threadpool: must report at least one worker.
        SMOKE_CHECK(ThreadpoolService::instance().workerCount() >= 1);

        // SysCb: deregister of a non-registered callback is safe.
        SysCbService::instance().deregisterCallback(nullptr);
        // No callbacks registered yet; issueEvent should return 0.
        SMOKE_CHECK(SysCbService::instance().issueEvent(0xDEAD) == 0);

        // Palette: with no ColorRegistry bound, returns fallback.
        const COLORREF fb = RGB(0xAA, 0xBB, 0xCC);
        COLORREF got_c = PaletteService::instance().queryColor(
            QString::fromLatin1("color.ml.list.bg"), fb);
        SMOKE_CHECK(got_c == fb);

        // Decode: stub returns empty.
        SMOKE_CHECK(DecodeService::instance().getMetadata(
            QString::fromLatin1("/tmp/x.mp3"),
            QString::fromLatin1("artist")).isEmpty());

        // 4. Cleanup of the smoke marker so subsequent qtamp
        //    runs don't see leftover keys.
        ConfigService::instance().setInt("smoke-phase-b", "marker", 0);
    }
};
static PhaseBSmoke s_smoke;

}  // anonymous
}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
