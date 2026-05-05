// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once
#include "esp_log.h"
#include "esp_err.h"

void _atomic_fail(esp_err_t rc, const char *file, int line, const char *function, const char *expression);

#define try(x) do {                                             \
    esp_err_t err_rc_ = (x);                                    \
    if (unlikely(err_rc_ != ESP_OK)) {                          \
        _atomic_fail(                                           \
            err_rc_, __FILE__, __LINE__, __ASSERT_FUNC, #x      \
        );                                                      \
    }                                                           \
} while (0)

#define nonull(x) do {                                          \
    if (unlikely(x == NULL)) {                                  \
        _atomic_fail(                                           \
            ESP_FAIL, __FILE__, __LINE__, __ASSERT_FUNC, #x     \
        );                                                      \
    }                                                           \
} while (0)
