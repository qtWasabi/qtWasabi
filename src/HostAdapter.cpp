// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

//
// HostAdapter — wires the embedder's WasabiQt::Host into the Wasabi
// service registry.  Wasabi's SystemObject / MediaCore / Config /
// Vis services route every script-callable into here, where we
// dispatch to the embedder's virtuals.
//
// Skeleton: TODO when src/Bootstrap.cpp brings the Wasabi services
// up to the point where we can register adapter callbacks against
// them.  At that point each WASABI_API_* dispatch lands in a method
// here, which calls the appropriate WasabiQt::Host virtual.
//

#include <WasabiQt/Host.h>

namespace WasabiQt {
// (Implementation lands here once Bootstrap.cpp has the Wasabi
// service registry initialised.  Keeping the file present so the
// link stage during early bootstrapping doesn't silently drop the
// translation unit.)
}
