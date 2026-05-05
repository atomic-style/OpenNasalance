// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "atomic_lcd_config.h"
#include "atomic_lcd_w5.h"
#include "atomic_lcd_cyd.h"
#include "atomic_lcd_ring.h"
#include "esp_err.h"

esp_err_t a_lcd_w5(a_lcd_t *lcd, bool (*cb)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *)) {
    (void)lcd; (void)cb;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t a_lcd_cyd(a_lcd_t *lcd, bool (*cb)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *)) {
    (void)lcd; (void)cb;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t a_lcd_ring(a_lcd_t *lcd, bool (*cb)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *)) {
    (void)lcd; (void)cb;
    return ESP_ERR_NOT_SUPPORTED;
}
