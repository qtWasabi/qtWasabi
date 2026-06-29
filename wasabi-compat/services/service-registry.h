// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once
//
// service-registry.h — thread-safe service registry.
//
// Each Wasabi service (LNG, APP, SKIN, …) implements a small C++
// interface (declared per-service in services/<svc>.h) and
// registers an instance with this registry at static-init or
// explicit call time.  gen_ml's `WASABI_API_SVC->
// service_getServiceByGuid(GUID)` ultimately routes here.
//
// The registry exposes the lookup primitives and bridges through a
// Dispatchable-shaped `api_service` subclass so gen_ml's existing
// call site
//   waServiceFactory *f = WASABI_API_SVC->service_getServiceByGuid(g);
// resolves to our registered factory transparently.
//

#include "service-guids.h"

#include <cstddef>

namespace qtWasabi {
namespace wasabi_compat {
namespace svc {

// ServiceObject — base for every concrete service.  Subclasses
// (LangService, AppService, SkinService, …) cast the receiver back
// to their interface inside the `interfacePtr()` call.  The BFC
// waServiceFactory dispatch is wired on top of this.
class ServiceObject {
public:
    virtual ~ServiceObject() = default;

    // Service identification.
    virtual GUID         guid() const = 0;
    virtual const char  *typeName() const { return "wasabi"; }
    virtual const char  *displayName() const { return "wasabi service"; }

    // Hand-off to the consuming plugin.  Wasabi convention returns
    // a typed-interface pointer the caller casts to e.g.
    // `api_application *`.  Concrete subclasses cast `this` (or a
    // sub-aggregate) to that type.
    virtual void        *interfacePtr() { return this; }
};

// Register / lookup / unregister.  Thread-safe.  Ownership stays
// with the caller — the registry holds a non-owning pointer.
// Re-registering the same GUID replaces the previous entry; a
// missing GUID returns null.
void          registerService(ServiceObject *svc);
ServiceObject *lookupService(GUID guid);
void          unregisterService(ServiceObject *svc);

// Iteration support — the dispatcher uses these to implement
// service_getNumServices / service_enumService.
std::size_t   serviceCount();
ServiceObject *serviceAt(std::size_t index);

// GUID equality — provided here for callers that don't want to
// include <cstring> just to compare two GUIDs.  Constexpr-friendly.
constexpr bool guidEq(const GUID &a, const GUID &b) {
    return a.Data1 == b.Data1 && a.Data2 == b.Data2 &&
           a.Data3 == b.Data3 &&
           a.Data4[0] == b.Data4[0] && a.Data4[1] == b.Data4[1] &&
           a.Data4[2] == b.Data4[2] && a.Data4[3] == b.Data4[3] &&
           a.Data4[4] == b.Data4[4] && a.Data4[5] == b.Data4[5] &&
           a.Data4[6] == b.Data4[6] && a.Data4[7] == b.Data4[7];
}

}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
