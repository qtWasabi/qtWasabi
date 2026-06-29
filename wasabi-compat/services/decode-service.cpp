// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "services/decode-service.h"

namespace qtWasabi {
namespace wasabi_compat {
namespace svc {
namespace {
struct AutoRegisterDecode {
    AutoRegisterDecode() { registerService(&DecodeService::instance()); }
};
static AutoRegisterDecode s_register;
}  // anonymous
}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
