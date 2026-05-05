// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include <stdint.h>

#include "atomic_lcd_config.h"
#include "esp_err.h"

typedef struct {
    float x;
    float y;
} vec2;

typedef struct {
    int count;
    vec2 pts[8];
} poly_t;

esp_err_t atomic_lcd_test_pride(void);
esp_err_t atomic_lcd_test_blank(void);
esp_err_t atomic_lcd_test_bars(void);
esp_err_t atomic_lcd_test_red(void);
