// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

//
// Bootstrap — brings up the Wasabi service registry against the
// user-supplied source tree (WASABI_SRC_DIR).  Wasabi's BFC / API
// / WND / SKIN init sequences happen here.
//
// Skeleton.  Concrete implementation comes after the first Wasabi
// source compile lands — this file marks the integration point.
//

namespace WasabiQt {

// One-shot service registry init.  Idempotent; safe to call from
// multiple WasabiQt::Skin constructors.
void bootstrapWasabiServices()
{
    // TODO:
    //   - InitBFC()
    //   - WASABI_API_*->serviceManager()->loadAllServices()
    //   - register Qt-side callbacks (paint, timer, mouse)
    //   - register HostAdapter as the playback / config provider
}

} // namespace WasabiQt
