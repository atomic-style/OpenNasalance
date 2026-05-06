
// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "atomic_ui.h"
#include "atomic_err.h"
#include "atomic_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "atomic_bits.h"
#include "lvgl.h"

static const char *TAG = "UI";

static lv_obj_t *lv_scr;
static lv_obj_t *lv_root;
static int32_t lv_h_res;
static int32_t lv_v_res;

// static lv_obj_t *lv_obj_bg;     // background blue rectangle
static lv_obj_t *lv_obj_status; // status label

static lv_image_dsc_t lv_logo_data; // image data
static lv_obj_t *lv_logo_obj;       // logo image object

extern const uint8_t splash_jpg_start[] asm("_binary_splash_jpg_start");
extern const uint8_t splash_jpg_end[] asm("_binary_splash_jpg_end");

static esp_err_t a_ui_logo_create(void) {
    lv_logo_data.header.magic = LV_IMAGE_HEADER_MAGIC;
    lv_logo_data.header.cf = LV_COLOR_FORMAT_RAW;
    lv_logo_data.header.w = lv_h_res;
    lv_logo_data.header.h = lv_v_res;
    lv_logo_data.data_size = splash_jpg_end - splash_jpg_start;
    lv_logo_data.data = splash_jpg_start;
    lv_logo_obj = lv_image_create(lv_scr);
    lv_image_set_src(lv_logo_obj, &lv_logo_data);
    lv_obj_center(lv_logo_obj);
    return ESP_OK;
}

static esp_err_t a_ui_main_create(void) {
    lv_obj_status = lv_label_create(lv_root);
    lv_label_set_text(lv_obj_status, "loading...");
    lv_obj_set_style_text_color(lv_obj_status, lv_color_white(), 0);
    lv_obj_set_pos(lv_obj_status, 0, 220);

    return ESP_OK;
}

static esp_err_t a_ui_init(void) {
    lv_lock();
    lv_scr = lv_screen_active();
    lv_root = lv_obj_create(lv_scr);
    lv_h_res = lv_obj_get_width(lv_scr);
    lv_v_res = lv_obj_get_height(lv_scr);

    lv_obj_remove_style_all(lv_root);
    lv_obj_set_size(lv_root, lv_h_res, lv_v_res);
    lv_obj_set_pos(lv_root, 0, 0);
    lv_obj_clear_flag(lv_root, LV_OBJ_FLAG_SCROLLABLE);

    try(a_ui_logo_create());
    try(a_ui_main_create());

    lv_obj_update_layout(lv_root);
    lv_unlock();

    return ESP_OK;
}

esp_err_t a_ui_clear(void) {
    lv_lock();
    lv_obj_add_flag(lv_logo_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lv_obj_status, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(lv_root);
    lv_unlock();
    return ESP_OK;
}

esp_err_t atomic_ui(void) {
    a_bits_wait(BIT_LVGL_READY);
    try(a_ui_init());

    return ESP_OK;
}