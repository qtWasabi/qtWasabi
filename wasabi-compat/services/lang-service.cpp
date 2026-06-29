// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// lang-service.cpp — register the language passthrough service
// with the wasabi-compat registry at static-init time.
//

#include "services/lang-service.h"

namespace qtWasabi {
namespace wasabi_compat {
namespace svc {
namespace {

struct AutoRegisterLang {
    AutoRegisterLang() {
        registerService(&LangService::instance());
    }
};
static AutoRegisterLang s_register;

}  // anonymous
}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
