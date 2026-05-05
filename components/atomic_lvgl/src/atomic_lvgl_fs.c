// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "atomic_lvgl_fs.h"

#include "atomic_bits.h"
#include "atomic_err.h"
#include "atomic_log.h"
#include "esp_err.h"
#include "lvgl.h"
#include <stdbool.h>

static const char *TAG = "atomic_lvgl_fs";

static esp_err_t a_lvgl_fs_dir(void) {
    lv_fs_dir_t dir;
    lv_fs_res_t res;
    res = lv_fs_dir_open(&dir, "/");
    if (res != LV_FS_RES_OK) {
        err(TAG, "lv_fs_dir_open() failed");
        return ESP_FAIL;
    }
    debug(TAG, "lvgl fs:");
    char fn[256];

    while (1) {
        res = lv_fs_dir_read(&dir, fn, sizeof(fn));
        if (res != LV_FS_RES_OK) {
            err(TAG, "lv_fs_dir_read() failed");
            return ESP_FAIL;
        }
        if (strlen(fn) == 0)
            break;
        info(TAG, "%s", fn);
    }
    lv_fs_dir_close(&dir);
    return ESP_OK;
}

esp_err_t a_lvgl_fs_init(void) {
    a_bits_wait(BIT_SD_READY);
    debug(TAG, "initializing file system driver");
    lv_fs_stdio_init();

    esp_err_t ok = a_lvgl_fs_dir();
    if (ok != ESP_OK) {
        err(TAG, "a_lvgl_fs_dir() failed");
        return ok;
    }
    a_bits_set(BIT_LVGL_FS_READY);

    return ESP_OK;
}