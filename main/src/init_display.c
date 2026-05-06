// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "init_display.h"

#include "atomic_err.h"
#include "atomic_lcd_jpeg.h"
#include "atomic_lcd_test.h"
#include "atomic_log.h"
#include "atomic_lvgl_jpeg.h"
#include "atomic_lvgl_test.h"
#include "config.h"
#include "esp_err.h"

static const char *TAG = "さ";

#ifdef ENABLE_LCD
#include "atomic_lcd.h"

static esp_err_t init_lcd(void) {
    try(a_lcd_init(LCD_ES3N28P));
    return ESP_OK;
}
#endif // ENABLE_LCD

#ifdef ENABLE_LVGL
#include "atomic_lvgl.h"
#include "atomic_lvgl_test.h"

static esp_err_t init_lvgl(void) {
    a_lvgl_cfg_t a_lvgl_cfg = {
        .h_res = a_lcd_get()->cfg->h_res,
        .v_res = a_lcd_get()->cfg->v_res,
        .sw_rotation = false,
    };
    if (a_lcd_get()->id == LCD_W5) {
        a_lvgl_cfg.sw_rotation = true;
    }
    try(atomic_lvgl_init(&a_lvgl_cfg));
    return ESP_OK;
}
#endif // ENABLE_LVGL

esp_err_t init_display(void) {

#ifdef ENABLE_LCD
    try(init_lcd());
    // try(atomic_lcd_test_blank());
    // try(atomic_lcd_test_pride());
    // try(atomic_lcd_test_bars());
#endif // ENABLE_LCD

#ifdef ENABLE_LVGL
    try(init_lvgl());
    // try(a_lvgl_jpeg("LCARSa.jpg"));
    // atomic_fucking_around_with_lcd_screen_graphics_and_fuck_up_the_graphics_library();
#endif // ENABLE_LVGL

    return ESP_OK;
}
