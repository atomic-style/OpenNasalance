// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atomic_lcd_config.h"
#include "esp_err.h"
#include "esp_lcd_types.h"

a_lcd_t	 *a_lcd_get(void);
bool	  a_lcd_cb(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event_data, void *user_ctx);
esp_err_t a_lcd_init(a_board_id_t id);
