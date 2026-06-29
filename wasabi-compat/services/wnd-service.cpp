// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "services/wnd-service.h"

namespace qtWasabi {
namespace wasabi_compat {
namespace svc {
namespace {
struct AutoRegisterWnd {
    AutoRegisterWnd() { registerService(&WndService::instance()); }
};
static AutoRegisterWnd s_register;
}  // anonymous
}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
