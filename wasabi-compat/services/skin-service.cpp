// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "services/skin-service.h"

namespace qtWasabi {
namespace wasabi_compat {
namespace svc {
namespace {
struct AutoRegisterSkin {
    AutoRegisterSkin() { registerService(&SkinService::instance()); }
};
static AutoRegisterSkin s_register;
}  // anonymous
}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
