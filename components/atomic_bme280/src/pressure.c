// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Experimental pressure readout for OpenNasalance (see old/ADDITION.md).
//
// Drives a Waveshare BME280 breakout sharing the FT6336G touch I²C bus and
// renders a rolling differential-pressure trace in the existing nasometer
// frame. Lives in its own component (atomic_bme280) so it can be revived on a
// future branch — gating ENABLE_BME280 in main/include/config.h routes init
// here instead of nasometer_init().

#include "pressure.h"

#include <math.h>
#include <string.h>

#include "atomic_log.h"
#include "atomic_touch.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "圧";

// ---------- Frame layout (matches nasometer.c) ----------
#define DISPLAY_W 320
#define DISPLAY_H 240
#define TEXT_TOP_H 18
#define TEXT_BOT_H 19
#define BUTTON_ROW_H 39
#define SPECTRO_H 80
#define SPECTRO_W DISPLAY_W

#define RULE1_Y (TEXT_TOP_H)
#define SPECTRO_A_Y (RULE1_Y + 1)
#define RULE2_Y (SPECTRO_A_Y + SPECTRO_H)
#define SPECTRO_B_Y (RULE2_Y + 1)
#define RULE3_Y (SPECTRO_B_Y + SPECTRO_H)
#define TEXT_BOT_Y (RULE3_Y + 1)
#define RULE4_Y (TEXT_BOT_Y + TEXT_BOT_H)
#define BUTTON_ROW_Y (RULE4_Y + 1)

#define BYTES_PER_PX 2 // RGB565

// ---------- BME280 register / field constants ----------
#define BME280_REG_ID         0xD0
#define BME280_REG_RESET      0xE0
#define BME280_REG_CTRL_HUM   0xF2
#define BME280_REG_STATUS     0xF3
#define BME280_REG_CTRL_MEAS  0xF4
#define BME280_REG_CONFIG     0xF5
#define BME280_REG_PRESS_MSB  0xF7
#define BME280_REG_CALIB_T_P  0x88 // 0x88..0xA1, 26 bytes T1..T3, P1..P9

#define BME280_ID             0x60
#define BME280_RESET_MAGIC    0xB6

// ctrl_meas: osrs_t<<5 | osrs_p<<2 | mode
//   osrs_t=001 (x1), osrs_p=011 (x4), mode=11 (normal) → 0x2F
#define BME280_CTRL_MEAS_VAL  0x2F
// config: t_sb<<5 | filter<<2 | spi3w_en
//   t_sb=010 (62.5 ms — irrelevant since we read on a software timer),
//   filter=000 (off), spi3w=0 → 0x40
#define BME280_CONFIG_VAL     0x40
// ctrl_hum: osrs_h=000 (skipped — humidity unused)
#define BME280_CTRL_HUM_VAL   0x00

// ---------- Display constants ----------
#define GRAPH_H SPECTRO_H
#define GRAPH_W SPECTRO_W
// One sample per pixel column → ring length matches graph width.
#define RING_LEN GRAPH_W
// Stats window in samples (rounded down — close enough at 25 Hz × 10 s = 250).
#define STATS_LEN (PRESSURE_SAMPLE_HZ * PRESSURE_STATS_WINDOW_SEC)

#define COL_GRAPH    0xFFE0 // yellow
#define COL_BG       0x0000 // black
#define COL_GRID     0x2104 // dim gray
#define COL_ZERO     0x4208 // medium gray (zero-line)

// ---------- BME280 calibration block ----------
typedef struct {
    uint16_t T1;
    int16_t  T2;
    int16_t  T3;
    uint16_t P1;
    int16_t  P2;
    int16_t  P3;
    int16_t  P4;
    int16_t  P5;
    int16_t  P6;
    int16_t  P7;
    int16_t  P8;
    int16_t  P9;
} bme280_calib_t;

static i2c_master_dev_handle_t s_dev = NULL;
static bme280_calib_t s_calib;
static int32_t s_t_fine;          // updated by temperature compensation, used by pressure
static double s_baseline_pa = 0;  // captured once at startup (and on Re-baseline)
static volatile bool s_request_rebaseline = false;

// ---------- LVGL widgets ----------
static lv_obj_t *s_scr_main   = NULL;
static lv_obj_t *s_canvas_top = NULL;
static lv_obj_t *s_canvas_bot = NULL;
static lv_obj_t *s_lbl_top    = NULL;
static lv_obj_t *s_lbl_bot    = NULL;
static lv_obj_t *s_btn_rebase = NULL;
static uint16_t *s_buf_top    = NULL;
static uint16_t *s_buf_bot    = NULL;

// ---------- Sample ring + stats ----------
static float    s_ring[RING_LEN];   // Pa differential, indexed by column
static int      s_ring_head = 0;    // next column to write at the right edge
static int      s_ring_filled = 0;  // total samples ever pushed (capped at RING_LEN for "is filled" tests)

static float    s_stats[STATS_LEN]; // last STATS_LEN samples for avg/max
static int      s_stats_head = 0;
static int      s_stats_count = 0;
static double   s_stats_sum = 0.0;

// ===================================================================
// BME280 driver (forced/normal mode register interface)
// ===================================================================

static esp_err_t bme280_read(uint8_t reg, uint8_t *buf, size_t len) {
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 100);
}

static esp_err_t bme280_write(uint8_t reg, uint8_t val) {
    uint8_t pkt[2] = {reg, val};
    return i2c_master_transmit(s_dev, pkt, 2, 100);
}

static esp_err_t bme280_load_calib(void) {
    uint8_t raw[26];
    esp_err_t r = bme280_read(BME280_REG_CALIB_T_P, raw, sizeof(raw));
    if (r != ESP_OK) return r;
    s_calib.T1 = (uint16_t)(raw[0]  | (raw[1]  << 8));
    s_calib.T2 = (int16_t) (raw[2]  | (raw[3]  << 8));
    s_calib.T3 = (int16_t) (raw[4]  | (raw[5]  << 8));
    s_calib.P1 = (uint16_t)(raw[6]  | (raw[7]  << 8));
    s_calib.P2 = (int16_t) (raw[8]  | (raw[9]  << 8));
    s_calib.P3 = (int16_t) (raw[10] | (raw[11] << 8));
    s_calib.P4 = (int16_t) (raw[12] | (raw[13] << 8));
    s_calib.P5 = (int16_t) (raw[14] | (raw[15] << 8));
    s_calib.P6 = (int16_t) (raw[16] | (raw[17] << 8));
    s_calib.P7 = (int16_t) (raw[18] | (raw[19] << 8));
    s_calib.P8 = (int16_t) (raw[20] | (raw[21] << 8));
    s_calib.P9 = (int16_t) (raw[22] | (raw[23] << 8));
    return ESP_OK;
}

// 32-bit fixed-point compensation from BME280 datasheet §4.2.3. Updates
// s_t_fine as a side effect — must be called before bme280_compensate_P.
static int32_t bme280_compensate_T(int32_t adc_T) {
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)s_calib.T1 << 1))) * ((int32_t)s_calib.T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)s_calib.T1)) *
                      ((adc_T >> 4) - ((int32_t)s_calib.T1))) >> 12) *
                    ((int32_t)s_calib.T3)) >> 14;
    s_t_fine = var1 + var2;
    return (s_t_fine * 5 + 128) >> 8; // hundredths of °C
}

// 64-bit fixed-point pressure compensation. Returns Pa in Q24.8.
static uint32_t bme280_compensate_P(int32_t adc_P) {
    int64_t var1 = ((int64_t)s_t_fine) - 128000;
    int64_t var2 = var1 * var1 * (int64_t)s_calib.P6;
    var2 = var2 + ((var1 * (int64_t)s_calib.P5) << 17);
    var2 = var2 + (((int64_t)s_calib.P4) << 35);
    var1 = ((var1 * var1 * (int64_t)s_calib.P3) >> 8) +
           ((var1 * (int64_t)s_calib.P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)s_calib.P1) >> 33;
    if (var1 == 0) return 0; // avoid divide-by-zero
    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)s_calib.P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)s_calib.P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)s_calib.P7) << 4);
    return (uint32_t)p;
}

static esp_err_t bme280_probe(i2c_master_bus_handle_t bus, uint8_t addr) {
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = 400000,
    };
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t r = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    if (r != ESP_OK) return r;

    uint8_t reg = BME280_REG_ID;
    uint8_t id = 0;
    r = i2c_master_transmit_receive(dev, &reg, 1, &id, 1, 100);
    if (r != ESP_OK || id != BME280_ID) {
        i2c_master_bus_rm_device(dev);
        return (r != ESP_OK) ? r : ESP_ERR_NOT_FOUND;
    }
    s_dev = dev;
    notice(TAG, "BME280 found at 0x%02X (id=0x%02X)", addr, id);
    return ESP_OK;
}

static esp_err_t bme280_init_chip(i2c_master_bus_handle_t bus) {
    if (bme280_probe(bus, 0x76) != ESP_OK &&
        bme280_probe(bus, 0x77) != ESP_OK) {
        err(TAG, "BME280 not responding on 0x76 or 0x77");
        return ESP_ERR_NOT_FOUND;
    }

    // Soft reset — chip needs ~2 ms to come back, then NVM copy ~2 ms more.
    if (bme280_write(BME280_REG_RESET, BME280_RESET_MAGIC) != ESP_OK) {
        warn(TAG, "BME280 soft reset write failed (continuing)");
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    // Wait for status.im_update (bit 0) to clear before configuring.
    for (int i = 0; i < 20; i++) {
        uint8_t st = 0;
        if (bme280_read(BME280_REG_STATUS, &st, 1) == ESP_OK && (st & 0x01) == 0) break;
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    if (bme280_load_calib() != ESP_OK) {
        err(TAG, "calibration read failed");
        return ESP_FAIL;
    }

    // ctrl_hum must be written before ctrl_meas to take effect (datasheet §5.4.3).
    if (bme280_write(BME280_REG_CTRL_HUM,  BME280_CTRL_HUM_VAL)  != ESP_OK ||
        bme280_write(BME280_REG_CONFIG,    BME280_CONFIG_VAL)    != ESP_OK ||
        bme280_write(BME280_REG_CTRL_MEAS, BME280_CTRL_MEAS_VAL) != ESP_OK) {
        err(TAG, "BME280 configuration write failed");
        return ESP_FAIL;
    }

    // First measurement takes one full cycle; wait for it before returning.
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

// Read raw 20-bit pressure + 20-bit temperature, return compensated pressure in Pa.
static esp_err_t bme280_read_pressure_pa(double *out_pa) {
    uint8_t buf[6];
    esp_err_t r = bme280_read(BME280_REG_PRESS_MSB, buf, 6);
    if (r != ESP_OK) return r;
    int32_t adc_P = ((int32_t)buf[0] << 12) | ((int32_t)buf[1] << 4) | (buf[2] >> 4);
    int32_t adc_T = ((int32_t)buf[3] << 12) | ((int32_t)buf[4] << 4) | (buf[5] >> 4);
    if (adc_P == 0x80000 || adc_T == 0x80000) return ESP_ERR_INVALID_RESPONSE;
    bme280_compensate_T(adc_T); // updates s_t_fine
    uint32_t p_q24_8 = bme280_compensate_P(adc_P);
    *out_pa = (double)p_q24_8 / 256.0;
    return ESP_OK;
}

// ===================================================================
// Stats ring (10-second avg/max)
// ===================================================================

static void stats_push(float v) {
    if (s_stats_count == STATS_LEN) {
        s_stats_sum -= s_stats[s_stats_head];
    }
    s_stats[s_stats_head] = v;
    s_stats_sum += v;
    s_stats_head = (s_stats_head + 1) % STATS_LEN;
    if (s_stats_count < STATS_LEN) s_stats_count++;
}

static void stats_compute(float *out_avg, float *out_max) {
    if (s_stats_count == 0) {
        *out_avg = 0.0f;
        *out_max = 0.0f;
        return;
    }
    *out_avg = (float)(s_stats_sum / s_stats_count);
    float m = s_stats[0];
    for (int i = 1; i < s_stats_count; i++) {
        if (s_stats[i] > m) m = s_stats[i];
    }
    *out_max = m;
}

static void stats_reset(void) {
    memset(s_stats, 0, sizeof(s_stats));
    s_stats_head  = 0;
    s_stats_count = 0;
    s_stats_sum   = 0.0;
}

// ===================================================================
// Graph rendering
// ===================================================================

static int pa_to_y(float v) {
    float t = (v - PRESSURE_DELTA_MIN_PA) /
              (PRESSURE_DELTA_MAX_PA - PRESSURE_DELTA_MIN_PA);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    int y = GRAPH_H - 1 - (int)(t * (GRAPH_H - 1));
    if (y < 0) y = 0;
    if (y >= GRAPH_H) y = GRAPH_H - 1;
    return y;
}

static void draw_pixel(uint16_t *buf, int x, int y, uint16_t color) {
    if (x < 0 || x >= GRAPH_W || y < 0 || y >= GRAPH_H) return;
    buf[y * GRAPH_W + x] = color;
}

static void draw_vline(uint16_t *buf, int x, int y0, int y1, uint16_t color) {
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (y0 < 0) y0 = 0;
    if (y1 >= GRAPH_H) y1 = GRAPH_H - 1;
    for (int y = y0; y <= y1; y++) draw_pixel(buf, x, y, color);
}

static void redraw_graph(void) {
    // Background + zero-line + 5-cmH₂O grid.
    for (int i = 0; i < GRAPH_W * GRAPH_H; i++) s_buf_top[i] = COL_BG;

    int y_zero = pa_to_y(0.0f);
    for (int x = 0; x < GRAPH_W; x++) {
        s_buf_top[y_zero * GRAPH_W + x] = COL_ZERO;
    }
    // Grid every 5 Pa across the configured range.
    for (float v = PRESSURE_DELTA_MIN_PA; v <= PRESSURE_DELTA_MAX_PA; v += 5.0f) {
        if (fabsf(v) < 0.01f) continue; // already drew zero
        int y = pa_to_y(v);
        for (int x = 0; x < GRAPH_W; x += 6) {
            s_buf_top[y * GRAPH_W + x] = COL_GRID;
        }
    }

    // Trace: walk RING_LEN samples in order, oldest → newest.
    int n = (s_ring_filled < RING_LEN) ? s_ring_filled : RING_LEN;
    if (n < 2) return;

    int start = (s_ring_filled < RING_LEN) ? 0 : s_ring_head;
    int prev_y = pa_to_y(s_ring[start]);
    int x_offset = RING_LEN - n; // right-justify partial fills

    for (int i = 1; i < n; i++) {
        int idx = (start + i) % RING_LEN;
        int y = pa_to_y(s_ring[idx]);
        int x = x_offset + i;
        draw_vline(s_buf_top, x, prev_y, y, COL_GRAPH);
        prev_y = y;
    }
}

// ===================================================================
// LVGL UI
// ===================================================================

static void btn_rebase_cb(lv_event_t *e) {
    (void)e;
    s_request_rebaseline = true;
    notice(TAG, "Re-baseline pressed");
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int x, int y,
                             int w, int h, lv_color_t bg, lv_event_cb_t cb) {
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);
    return btn;
}

static void add_rule(lv_obj_t *scr, int y) {
    lv_obj_t *r = lv_obj_create(scr);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, DISPLAY_W, 1);
    lv_obj_set_pos(r, 0, y);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, lv_color_make(96, 96, 96), 0);
}

static void build_ui(void) {
    lv_lock();
    lv_obj_t *scr = lv_screen_active();
    s_scr_main = scr;
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);

    // Top title — y-axis range and units.
    s_lbl_top = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_top, lv_color_white(), 0);
    lv_label_set_text(s_lbl_top, "Pressure (Pa, -10..+40)");
    lv_obj_set_size(s_lbl_top, DISPLAY_W, TEXT_TOP_H);
    lv_obj_set_pos(s_lbl_top, 0, 2);
    lv_obj_set_style_text_align(s_lbl_top, LV_TEXT_ALIGN_CENTER, 0);

    // Top canvas — rolling pressure trace.
    s_canvas_top = lv_canvas_create(scr);
    lv_canvas_set_buffer(s_canvas_top, s_buf_top, GRAPH_W, GRAPH_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas_top, 0, SPECTRO_A_Y);
    lv_canvas_fill_bg(s_canvas_top, lv_color_black(), LV_OPA_COVER);

    // Bottom canvas — idle, kept so the frame layout matches the nasometer.
    s_canvas_bot = lv_canvas_create(scr);
    lv_canvas_set_buffer(s_canvas_bot, s_buf_bot, GRAPH_W, GRAPH_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas_bot, 0, SPECTRO_B_Y);
    lv_canvas_fill_bg(s_canvas_bot, lv_color_make(0x08, 0x08, 0x08), LV_OPA_COVER);

    add_rule(scr, RULE1_Y);
    add_rule(scr, RULE2_Y);
    add_rule(scr, RULE3_Y);
    add_rule(scr, RULE4_Y);

    // Bottom readout — Base / Avg / Max.
    s_lbl_bot = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_bot, lv_color_white(), 0);
    lv_label_set_text(s_lbl_bot, "Base ----.- hPa  Avg --- Max ---");
    lv_obj_set_size(s_lbl_bot, DISPLAY_W, TEXT_BOT_H);
    lv_obj_set_pos(s_lbl_bot, 4, TEXT_BOT_Y);

    // Single Re-baseline button — captures the current absolute reading as
    // the new zero reference and clears the avg/max window.
    s_btn_rebase = make_button(scr, "Re-baseline", 8, BUTTON_ROW_Y + 2,
                               140, 35, lv_color_make(0x4a, 0x4a, 0x4a),
                               btn_rebase_cb);
    lv_unlock();
}

static void refresh_status_label(double base_hpa, float avg_pa, float max_pa) {
    if (!s_lbl_bot) return;
    lv_label_set_text_fmt(s_lbl_bot, "Base %6.1f hPa  Avg %+5.1f  Max %+5.1f Pa",
                          base_hpa, avg_pa, max_pa);
}

// ===================================================================
// Sampling task
// ===================================================================

static void capture_baseline(void) {
    // Average a few reads to smooth out one-cycle jitter.
    double acc = 0.0;
    int    ok  = 0;
    for (int i = 0; i < 8; i++) {
        double pa;
        if (bme280_read_pressure_pa(&pa) == ESP_OK) {
            acc += pa;
            ok++;
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
    if (ok > 0) {
        s_baseline_pa = acc / ok;
        notice(TAG, "baseline = %.2f hPa (%d reads)", s_baseline_pa / 100.0, ok);
    } else {
        warn(TAG, "baseline capture failed — no successful reads");
    }
    stats_reset();
    memset(s_ring, 0, sizeof(s_ring));
    s_ring_head   = 0;
    s_ring_filled = 0;
}

static void pressure_task(void *arg) {
    (void)arg;
    notice(TAG, "pressure task running at %d Hz", PRESSURE_SAMPLE_HZ);

    capture_baseline();

    const TickType_t period = pdMS_TO_TICKS(1000 / PRESSURE_SAMPLE_HZ);
    TickType_t last = xTaskGetTickCount();
    int ui_tick = 0;

    while (1) {
        vTaskDelayUntil(&last, period);

        if (s_request_rebaseline) {
            s_request_rebaseline = false;
            capture_baseline();
            continue;
        }

        double pa;
        if (bme280_read_pressure_pa(&pa) != ESP_OK) {
            warn(TAG, "BME280 read failed");
            continue;
        }
        float delta_pa = (float)(pa - s_baseline_pa);

        s_ring[s_ring_head] = delta_pa;
        s_ring_head = (s_ring_head + 1) % RING_LEN;
        if (s_ring_filled < RING_LEN) s_ring_filled++;
        stats_push(delta_pa);

        lv_lock();
        redraw_graph();
        lv_obj_invalidate(s_canvas_top);

        // ~5 Hz label refresh — text update is cheap but we don't need 25 Hz.
        if (++ui_tick >= 5) {
            ui_tick = 0;
            float avg, mx;
            stats_compute(&avg, &mx);
            refresh_status_label(s_baseline_pa / 100.0, avg, mx);
        }
        lv_unlock();
    }
}

// ===================================================================
// Public init
// ===================================================================

esp_err_t pressure_init(void) {
    notice(TAG, "pressure_init()");

    i2c_master_bus_handle_t bus = atomic_touch_get_bus();
    if (!bus) {
        err(TAG, "no I²C bus available — atomic_touch must init first");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t r = bme280_init_chip(bus);
    if (r != ESP_OK) return r;

    uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    s_buf_top = heap_caps_malloc(GRAPH_W * GRAPH_H * BYTES_PER_PX, caps);
    s_buf_bot = heap_caps_malloc(GRAPH_W * GRAPH_H * BYTES_PER_PX, caps);
    if (!s_buf_top || !s_buf_bot) {
        warn(TAG, "PSRAM canvas alloc failed; trying internal");
        if (!s_buf_top) s_buf_top = heap_caps_malloc(GRAPH_W * GRAPH_H * BYTES_PER_PX, MALLOC_CAP_8BIT);
        if (!s_buf_bot) s_buf_bot = heap_caps_malloc(GRAPH_W * GRAPH_H * BYTES_PER_PX, MALLOC_CAP_8BIT);
        if (!s_buf_top || !s_buf_bot) {
            err(TAG, "canvas allocation failed");
            return ESP_ERR_NO_MEM;
        }
    }
    memset(s_buf_top, 0, GRAPH_W * GRAPH_H * BYTES_PER_PX);
    memset(s_buf_bot, 0, GRAPH_W * GRAPH_H * BYTES_PER_PX);

    build_ui();

    BaseType_t br = xTaskCreatePinnedToCore(pressure_task, "pressure", 6144, NULL, 4, NULL, 0);
    if (br != pdPASS) {
        err(TAG, "failed to create pressure task");
        return ESP_FAIL;
    }
    return ESP_OK;
}
