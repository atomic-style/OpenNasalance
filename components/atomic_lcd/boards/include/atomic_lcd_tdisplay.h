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

typedef struct {
	uint32_t addr;
	uint8_t	 param[20];
	uint32_t len;
} lcd_cmd_t;

esp_err_t a_lcd_tdisplay(a_lcd_t *lcd, bool (*cb)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *));