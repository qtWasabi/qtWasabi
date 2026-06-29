// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once
//
// smoke-check.h — replacement for `assert()` in our static-init
// smoke tests.  Release builds strip `assert()` arguments under
// NDEBUG; that turns smoke tests into compile-time-only checks
// (the assert wrapper made the actual function call disappear).
//
// `SMOKE_CHECK(expr)` always evaluates `expr` and aborts with a
// printf + abort if false, regardless of NDEBUG.  The macro is
// safe to use anywhere an assert would go.
//

#include <cstdio>
#include <cstdlib>

#define SMOKE_CHECK(expr)                                              \
    do {                                                                \
        if (!(expr)) {                                                  \
            std::fprintf(stderr,                                        \
                "[wasabi-compat-smoke] FAIL: %s at %s:%d\n",            \
                #expr, __FILE__, __LINE__);                             \
            std::abort();                                               \
        }                                                               \
    } while (0)
