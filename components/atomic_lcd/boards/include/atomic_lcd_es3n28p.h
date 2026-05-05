// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include "atomic_lcd_config.h"
#include "esp_err.h"

// ShenZhen QDtech ES3C28P / ES3N28P 2.8" IPS ESP32-S3 Display Module.
// ILI9341V controller, 240x320, 4-line SPI. Reset line is tied to CHIP_PU
// (shared with ESP32-S3 reset), so the panel has no separate reset GPIO.
esp_err_t a_lcd_es3n28p(a_lcd_t *lcd,
                        bool (*cb)(esp_lcd_panel_io_handle_t,
                                   esp_lcd_panel_io_event_data_t *, void *));
