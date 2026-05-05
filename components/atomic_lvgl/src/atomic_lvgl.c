// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "atomic_lvgl.h"

#include "atomic_bits.h"
#include "atomic_err.h"
#include "atomic_lcd.h"
#include "atomic_lcd_config.h"
#include "atomic_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lv_conf.h"
#include "lvgl.h"

#include "atomic_lvgl_fs.h"

#define LV_TASK_STACK 16384 // WAS 32768
#define LV_TASK_PRIO 4
#define LV_TICK_PERIOD_MS 1
#define JPEG_QUEUE_SIZE 4

static const char *TAG = "㐐";

static a_lcd_t *s_a_lcd;
static lv_display_t *s_lv_display;
static lv_display_render_mode_t s_render_mode;

static uint32_t s_buffer_size;
static uint16_t *s_draw_buffer;
static uint16_t *s_rotation_buffer;

static uint32_t s_h_res;
static uint32_t s_v_res;

static bool s_sw_rotation;
static bool s_debug = false;

QueueHandle_t s_jpeg_queue = NULL;
TaskHandle_t s_lvgl_task_handle = NULL;

static void lvgl_flush_cb_partial(lv_display_t *lv_disp, const lv_area_t *area, uint8_t *px_map) {
    if (s_debug) {
        debug(TAG, "flush_cb: x1=%d, y1=%d, x2=%d, y2=%d", area->x1, area->y1 + 1, area->x2, area->y2 + 1);
    }
    esp_lcd_panel_draw_bitmap(s_a_lcd->panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    xSemaphoreTake(s_a_lcd->sem, portMAX_DELAY);
    lv_display_flush_ready(lv_disp);
}

static void lvgl_flush_cb_rotate(lv_display_t *lv_display, const lv_area_t *area, uint8_t *color_p) {
    if (s_debug) {
        debug(TAG, "flush_cb_rotate: x1=%d, y1=%d, x2=%d, y2=%d", area->x1, area->y1 + 1, area->x2, area->y2 + 1);
    }
    uint16_t *src_pixels = (uint16_t *)color_p;
    uint16_t *dst_pixels = s_rotation_buffer;
    const int area_x1 = area->x1;
    const int area_y1 = area->y1;
    const int area_x2 = area->x2;
    const int area_y2 = area->y2;
    const int area_width = area_x2 - area_x1 + 1;
    const int area_height = area_y2 - area_y1 + 1;
    const int rotated_width = area_height;
    const int rotated_height = area_width;
    const int logical_height = s_v_res;
    const int min_px = logical_height - 1 - area_y2;
    const int min_py = area_x1;

    for (int ly = area_y1; ly <= area_y2; ly++) {
        int local_y = ly - area_y1;
        int src_row_start = local_y * area_width;

        for (int lx = area_x1; lx <= area_x2; lx++) {
            int local_x = lx - area_x1;
            int src_index = src_row_start + local_x;
            uint16_t src_color = src_pixels[src_index];
            int px = logical_height - 1 - ly; // panel X
            int py = lx;                      // panel Y
            int dx = px - min_px;             // 0 .. rotated_width-1
            int dy = py - min_py;             // 0 .. rotated_height-1
            int dst_index = dy * rotated_width + dx;
            dst_pixels[dst_index] = src_color;
        }
    }

    int panel_x_start = min_px;
    int panel_y_start = min_py;
    int panel_x_end = panel_x_start + rotated_width;
    int panel_y_end = panel_y_start + rotated_height;

    // lv_draw_sw_rgb565_swap(dst_pixels, rotated_width * rotated_height);
    try(esp_lcd_panel_draw_bitmap(s_a_lcd->panel, panel_x_start, panel_y_start, panel_x_end, panel_y_end, dst_pixels));
    xSemaphoreTake(s_a_lcd->sem, portMAX_DELAY);
    lv_display_flush_ready(lv_display);
}

static void lv_tick_cb(void *arg) {
    (void)arg;
    lv_tick_inc(LV_TICK_PERIOD_MS);
}

static void atomic_lvgl_task(void *arg) {
    warn(TAG, "atomic_lvgl_task() - starting");
    s_lvgl_task_handle = xTaskGetCurrentTaskHandle();

    s_lv_display = lv_display_create(s_h_res, s_v_res);
    if (s_lv_display == NULL) {
        err(TAG, "lv_display_create() failed - returning without setting BIT_LVGL_READY");
        return;
    }

    lv_display_set_color_format(s_lv_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_lv_display, s_draw_buffer, NULL, s_buffer_size, s_render_mode);
    lv_display_set_flush_cb(s_lv_display, (s_sw_rotation) ? lvgl_flush_cb_rotate : lvgl_flush_cb_partial);

    esp_timer_create_args_t timer_args = {.callback = &lv_tick_cb, .name = "lv_tick"};
    esp_timer_handle_t periodic_timer;

    esp_err_t ret = esp_timer_create(&timer_args, &periodic_timer);
    if (ret != ESP_OK) {
        err(TAG, "esp_timer_create() failed: %s - returning without setting BIT_LVGL_READY", esp_err_to_name(ret));
        return;
    }

    ret = esp_timer_start_periodic(periodic_timer, LV_TICK_PERIOD_MS * 1000);
    if (ret != ESP_OK) {
        err(TAG, "esp_timer_start_periodic() failed: %s - returning without setting BIT_LVGL_READY",
            esp_err_to_name(ret));
        return;
    }

    if (a_bits(BIT_SD_READY)) {
        info(TAG, "a_lvgl_fs_init() - init filesystem");
        esp_err_t ret = a_lvgl_fs_init();
        if (ret != ESP_OK) {
            err(TAG, "a_lvgl_fs_init() failed");
            return;
        } else {
            notice(TAG, "a_lvgl_fs_init() succeeded");
        }

        s_jpeg_queue = xQueueCreate(JPEG_QUEUE_SIZE, sizeof(char *));
        if (!s_jpeg_queue) {
            err(TAG, "Failed to create JPEG queue");
            return;
        }
    } else {
        err(TAG, "BIT_SD_READY not received -- skipping filesystem initialization");
    }

    a_bits_set(BIT_LVGL_READY);

    while (1) {
        if (a_bits(BIT_SD_READY)) {
            // Check for JPEG load requests
            char *filename = NULL;
            if (xQueueReceive(s_jpeg_queue, &filename, 0) == pdTRUE) {
                if (filename) {
                    extern esp_err_t a_lvgl_jpeg_internal(const char *filename);
                    esp_err_t ret = a_lvgl_jpeg_internal(filename);
                    if (ret != ESP_OK) {
                        err(TAG, "Failed to load JPEG: %s", filename);
                    }
                    free(filename);
                }
            }
        }

        lv_task_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t atomic_lvgl_init(a_lvgl_cfg_t *a_lvgl_cfg) {
    debug(TAG, "atomic_lvgl_init()");
    a_bits_wait(BIT_LCD_READY);
    s_draw_buffer = NULL;
    s_rotation_buffer = NULL;

#ifndef LV_SANITY_CHECK
    warn(TAG, "LV_SANITY_CHECK failed.");
    return ESP_ERR_INVALID_STATE;
#endif

    lv_init();
    s_a_lcd = a_lcd_get();
    s_sw_rotation = a_lvgl_cfg->sw_rotation;
    s_h_res = (s_sw_rotation) ? a_lvgl_cfg->v_res : s_a_lcd->cfg->h_res;
    s_v_res = (s_sw_rotation) ? a_lvgl_cfg->h_res : s_a_lcd->cfg->v_res;
    info(TAG, "atomic_lvgl_init() h_res: %d, v_res: %d, color depth: %d", s_h_res, s_v_res, s_a_lcd->cfg->bpp);

    uint32_t caps = MALLOC_CAP_8BIT;
    if (s_sw_rotation) {
        s_buffer_size = s_h_res * s_v_res * s_a_lcd->cfg->bpp / 8;
        warn(TAG, "Software rotation enabled. Two full-screen buffers required. Size: %d", s_buffer_size);
    } else {
        s_buffer_size = s_a_lcd->cfg->max_transfer_sz;
        caps = MALLOC_CAP_8BIT | MALLOC_CAP_DMA;
        warn(TAG, "Software rotation disabled. Allocating DMA buffer to max transfer size: %d", s_buffer_size);
    }

    debug(TAG, "atomic_lvgl_init() - allocating draw buffer");
    s_draw_buffer = (uint16_t *)heap_caps_malloc(s_buffer_size, caps);
    if (!s_draw_buffer) {
        warn(TAG, "s_draw_buffer DMA allocation failed, trying 8BIT");
        caps = MALLOC_CAP_8BIT;
        s_draw_buffer = (uint16_t *)heap_caps_malloc(s_buffer_size, caps);
        if (s_draw_buffer) {
            notice(TAG, "s_draw_buffer 8BIT allocation succeeded");
            memset(s_draw_buffer, 0, s_buffer_size);
        } else {
            warn(TAG, "s_draw_buffer 8BIT allocation failed");
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_draw_buffer) {
        info(TAG, "s_draw_buffer allocation succeeded");
    } else {
        warn(TAG, "s_draw_buffer allocation failed");
        return ESP_ERR_NO_MEM;
    }

    if (s_sw_rotation) {
        info(TAG, "allocating rotation buffer");
        s_rotation_buffer = (uint16_t *)heap_caps_malloc(s_buffer_size, MALLOC_CAP_8BIT);
        if (!s_rotation_buffer) {
            err(TAG, "s_rotation_buffer allocation failed");
            return ESP_ERR_NO_MEM;
        }
    }

    s_render_mode = LV_DISPLAY_RENDER_MODE_PARTIAL; // LV_DISPLAY_RENDER_MODE_FULL

    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    if (largest_block < LV_TASK_STACK) {
        err(TAG, "Insufficient internal RAM: need %d bytes but only %zu bytes available in largest block",
            LV_TASK_STACK, largest_block);
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ret =
        xTaskCreatePinnedToCore(atomic_lvgl_task, "atomic_lvgl_task", LV_TASK_STACK, NULL, LV_TASK_PRIO, NULL, 1);
    if (ret != pdPASS) {
        err(TAG, "xTaskCreatePinnedToCore() failed - ret=%d, stack_size=%d, prio=%d, core=1", ret, LV_TASK_STACK,
            LV_TASK_PRIO);
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(200));
    a_bits_wait(BIT_LVGL_READY);
    info(TAG, "atomic_lvgl_init() done");
    return ESP_OK;
}
