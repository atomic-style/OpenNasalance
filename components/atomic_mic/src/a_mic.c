// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "a_mic.h"

#include "atomic_err.h"
#include "atomic_log.h"
#include "driver/i2s_common.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "㇐";

#define T5837_BUF_SAMPLES 1024
#define T5837_REPORT_READS 43

static i2s_chan_handle_t s_rx_chan = NULL;   // INMP441 master, reads data_1
static i2s_chan_handle_t s_rx_chan_2 = NULL; // INMP441 slave, reads data_2
static i2s_chan_handle_t s_t5837_chan = NULL;

esp_err_t a_mic_441_init(a_mic_441_config_t *cfg) {
    notice(TAG, "init INMP441  bclk=GPIO%d ws=GPIO%d din1=GPIO%d din2=GPIO%d  %dHz", cfg->pin_clk,
           cfg->pin_ws, cfg->pin_data_1, cfg->pin_data_2, cfg->sample_rate);

    // Master channel (I2S_NUM_0): generates CLK & WS, reads data_1
    i2s_chan_config_t chan_cfg_m = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    try(i2s_new_channel(&chan_cfg_m, NULL, &s_rx_chan));

    // Slave channel (I2S_NUM_1): follows CLK & WS from master, reads data_2
    i2s_chan_config_t chan_cfg_s = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_SLAVE);
    try(i2s_new_channel(&chan_cfg_s, NULL, &s_rx_chan_2));

    // Philips/I2S standard mode - INMP441 outputs 24-bit audio MSB-first
    // in a 32-bit frame; lower 8 bits are zero.
    i2s_std_slot_config_t slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO);
    slot_cfg.slot_mask = I2S_STD_SLOT_LEFT; // L/R tied to GND → left channel

    i2s_std_config_t std_cfg_m = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(cfg->sample_rate),
        .slot_cfg = slot_cfg,
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = cfg->pin_clk,
            .ws = cfg->pin_ws,
            .dout = I2S_GPIO_UNUSED,
            .din = cfg->pin_data_1,
            .invert_flags = {0},
        },
    };

    // Slave shares the same CLK/WS GPIOs (inputs via GPIO matrix), reads data_2
    i2s_std_config_t std_cfg_s = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(cfg->sample_rate), // ignored in slave mode
        .slot_cfg = slot_cfg,
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = cfg->pin_clk,
            .ws = cfg->pin_ws,
            .dout = I2S_GPIO_UNUSED,
            .din = cfg->pin_data_2,
            .invert_flags = {0},
        },
    };

    try(i2s_channel_init_std_mode(s_rx_chan, &std_cfg_m));
    try(i2s_channel_init_std_mode(s_rx_chan_2, &std_cfg_s));
    try(i2s_channel_enable(s_rx_chan));
    try(i2s_channel_enable(s_rx_chan_2));

    notice(TAG, "INMP441 dual running");
    return ESP_OK;
}

esp_err_t a_mic_441_read_dual(int32_t *buf1, int32_t *buf2, size_t frames, int timeout_ms) {
    if (s_rx_chan == NULL || s_rx_chan_2 == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    TickType_t to = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    size_t want = frames * sizeof(int32_t);
    size_t got1 = 0, got2 = 0;

    if (buf1) {
        esp_err_t r = i2s_channel_read(s_rx_chan, buf1, want, &got1, to);
        if (r != ESP_OK) return r;
        if (got1 != want) return ESP_ERR_TIMEOUT;
    }
    if (buf2) {
        esp_err_t r = i2s_channel_read(s_rx_chan_2, buf2, want, &got2, to);
        if (r != ESP_OK) return r;
        if (got2 != want) return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

// --- T5837 (PDM) support retained for later use -----------------------------

static void level_bar(int level, char *out) {
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    int filled = level * 20 / 100;
    int i = 0;
    out[i++] = '[';
    for (int j = 0; j < 20; j++) out[i++] = (j < filled) ? '#' : ' ';
    out[i++] = ']';
    out[i] = '\0';
}

// Stereo PDM monitor: Mic A = left (SEL→GND), Mic B = right (SEL→VDD).
// Interleaved layout from I2S: [A0, B0, A1, B1, ...]
static void pdm_monitor_task(void *arg) {
    i2s_chan_handle_t chan = (i2s_chan_handle_t)arg;
    const int BUF_FRAMES = T5837_BUF_SAMPLES;
    int16_t *buf =
        heap_caps_malloc(BUF_FRAMES * 2 * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) {
        err(TAG, "failed to allocate audio buffer");
        vTaskDelete(NULL);
        return;
    }

    size_t bytes_read = 0;
    int32_t peak_a = 0, peak_b = 0;
    int reads = 0;
    bool got_signal = false;

    notice(TAG, "PDM stereo monitor task started");

    while (1) {
        esp_err_t ret =
            i2s_channel_read(chan, buf, BUF_FRAMES * 2 * sizeof(int16_t), &bytes_read, 2000);
        if (ret != ESP_OK) {
            warn(TAG, "read: %s", esp_err_to_name(ret));
            continue;
        }
        int frames = (int)(bytes_read / (2 * sizeof(int16_t)));
        for (int i = 0; i < frames; i++) {
            int32_t a = buf[i * 2];
            int32_t b = buf[i * 2 + 1];
            int32_t abs_a = (a < 0) ? -a : a;
            int32_t abs_b = (b < 0) ? -b : b;
            if (abs_a > peak_a) peak_a = abs_a;
            if (abs_b > peak_b) peak_b = abs_b;
        }
        if (!got_signal && (peak_a > 0 || peak_b > 0)) {
            got_signal = true;
            notice(TAG, "signal detected - data is flowing");
        }
        reads++;
        if (reads >= T5837_REPORT_READS) {
            reads = 0;
            int level_a = (int)(peak_a * 100 / 32767);
            int level_b = (int)(peak_b * 100 / 32767);
            char bar_a[32], bar_b[32];
            level_bar(level_a, bar_a);
            level_bar(level_b, bar_b);
            info(TAG, "A:%s %3d%%  B:%s %3d%%", bar_a, level_a, bar_b, level_b);
            peak_a = 0;
            peak_b = 0;
        }
    }
}

esp_err_t a_mic_t5837_init(a_mic_t5837_config_t *cfg) {
    notice(TAG, "init T5837 (PDM)  clk=GPIO%d din=GPIO%d  %dHz  buf_samples=%d", cfg->pin_clk,
           cfg->pin_data, cfg->sample_rate, cfg->buf_samples);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    try(i2s_new_channel(&chan_cfg, NULL, &s_t5837_chan));

    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(cfg->sample_rate),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg =
            {
                .clk = cfg->pin_clk,
                .din = cfg->pin_data,
                .invert_flags = {0},
            },
    };

    try(i2s_channel_init_pdm_rx_mode(s_t5837_chan, &pdm_cfg));
    try(i2s_channel_enable(s_t5837_chan));

    notice(TAG, "T5837 running");

    BaseType_t ret = xTaskCreate(pdm_monitor_task, "t5837_monitor", 8192, s_t5837_chan, 5, NULL);
    if (ret != pdPASS) {
        err(TAG, "failed to create T5837 monitor task");
        return ESP_FAIL;
    }

    return ESP_OK;
}
