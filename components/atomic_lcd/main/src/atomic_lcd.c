// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "atomic_lcd.h"

#include "atomic_err.h"
// #include "atomic_gpio.h"
#include "atomic_bits.h"
#include "atomic_lcd_config.h"
#include "atomic_lcd_cyd.h"
#include "atomic_lcd_es3n28p.h"
#include "atomic_lcd_ring.h"
#include "atomic_lcd_tdisplay.h"
#include "atomic_lcd_test.h"
#include "atomic_lcd_w5.h"
#include "atomic_log.h"
#include "esp_lcd_types.h"
#include "freertos/task.h"

static const char *TAG = "䋧";

static a_lcd_t *s_lcd;
static SemaphoreHandle_t s_sem;

static int (*a_lcd_fn(a_board_id_t id))(a_lcd_t *,
                                        bool (*)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *)) {
    switch (id) {
    case LCD_W5:
        return a_lcd_w5;
    case LCD_TDISPLAY:
        return a_lcd_tdisplay;
    case LCD_CYD:
        return a_lcd_cyd;
    case LCD_RING:
        return a_lcd_ring;
    case LCD_ES3N28P:
        return a_lcd_es3n28p;
    default:
        return NULL;
    }
}

bool a_lcd_cb(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event_data, void *user_ctx) {
    BaseType_t hpw = pdFALSE;
    xSemaphoreGiveFromISR(s_sem, &hpw);
    return hpw == pdTRUE;
}

a_lcd_t *a_lcd_get(void) { return s_lcd; }

esp_err_t a_lcd_init(a_board_id_t id) {
    debug(TAG, "a_lcd_init(%d)", id);
    /*
    if (!id) {
        err(TAG, "invalid board id");
        return ESP_ERR_INVALID_ARG;
    }
    */
    esp_err_t ok = ESP_FAIL;

    if (s_lcd) {
        err(TAG, "s_lcd already initialized");
        return ok;
    }

    debug(TAG, "allocating lcd struct");
    s_lcd = calloc(1, sizeof(a_lcd_t));
    s_sem = xSemaphoreCreateBinary();
    s_lcd->sem = s_sem;
    debug(TAG, "calling a_lcd_fn(%d)", id);
    ok = a_lcd_fn(id)(s_lcd, a_lcd_cb);
    debug(TAG, "a_lcd_fn(%d) returned %d", id, ok);

    a_bits_set(BIT_LCD_READY);

    return ok;
}
