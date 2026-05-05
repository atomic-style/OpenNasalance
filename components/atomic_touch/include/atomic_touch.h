// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

typedef struct {
    uint8_t i2c_port;
    int pin_sda;
    int pin_scl;
    int pin_rst;
    int pin_int;
    // Native panel dimensions as the touch IC reports them (portrait).
    uint16_t native_w;
    uint16_t native_h;
    // Rotation applied by the display, in degrees CCW: 0, 90, 180, or 270.
    uint16_t rotation_ccw;
} atomic_touch_cfg_t;

typedef struct {
    int16_t x;
    int16_t y;
    bool pressed;
} atomic_touch_point_t;

esp_err_t atomic_touch_init(const atomic_touch_cfg_t *cfg);

// Returns the current touch point already transformed into display coordinates.
// pressed=false when no touch is active. Safe to call from any task.
esp_err_t atomic_touch_read(atomic_touch_point_t *out);

// Returns the I²C master bus the touch driver created so other peripherals on
// the same SDA/SCL pair (e.g. an env sensor on the P4 header) can share it.
// Returns NULL until atomic_touch_init() succeeds.
i2c_master_bus_handle_t atomic_touch_get_bus(void);
