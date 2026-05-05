// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef struct a_lvgl_cfg_s {
    int h_res;
    int v_res;
    bool sw_rotation;
} a_lvgl_cfg_t;

esp_err_t atomic_lvgl_init(a_lvgl_cfg_t *a_lvgl_cfg);