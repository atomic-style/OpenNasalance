// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "atomic_touch.h"

#include <string.h>

#include "atomic_err.h"
#include "atomic_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "䒻";

#define FT6336G_ADDR 0x38
#define FT6336G_REG_DEV_MODE 0x00
#define FT6336G_REG_TD_STATUS 0x02
#define FT6336G_REG_P1_XH 0x03

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static uint16_t s_native_w = 240;
static uint16_t s_native_h = 320;
static uint16_t s_rot = 0;

static esp_err_t read_regs(uint8_t reg, uint8_t *buf, size_t len) {
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 50);
}

esp_err_t atomic_touch_init(const atomic_touch_cfg_t *cfg) {
    if (!cfg) return ESP_ERR_INVALID_ARG;
    notice(TAG, "init FT6336G  i2c%d sda=%d scl=%d rst=%d int=%d  rot=%d",
           cfg->i2c_port, cfg->pin_sda, cfg->pin_scl, cfg->pin_rst, cfg->pin_int, cfg->rotation_ccw);

    s_native_w = cfg->native_w ? cfg->native_w : 240;
    s_native_h = cfg->native_h ? cfg->native_h : 320;
    s_rot = cfg->rotation_ccw % 360;

    if (cfg->pin_rst >= 0) {
        gpio_config_t rst_cfg = {
            .pin_bit_mask = 1ULL << cfg->pin_rst,
            .mode = GPIO_MODE_OUTPUT,
        };
        try(gpio_config(&rst_cfg));
        gpio_set_level(cfg->pin_rst, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(cfg->pin_rst, 1);
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = cfg->i2c_port,
        .sda_io_num = cfg->pin_sda,
        .scl_io_num = cfg->pin_scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    try(i2c_new_master_bus(&bus_cfg, &s_bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = FT6336G_ADDR,
        .scl_speed_hz = 400000,
    };
    try(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev));

    // Put the controller in polling mode by setting DEV_MODE to 0.
    uint8_t mode_set[2] = {FT6336G_REG_DEV_MODE, 0x00};
    esp_err_t r = i2c_master_transmit(s_dev, mode_set, 2, 50);
    if (r != ESP_OK) {
        warn(TAG, "FT6336G DEV_MODE write failed: %s (continuing)", esp_err_to_name(r));
    }

    notice(TAG, "FT6336G ready");
    return ESP_OK;
}

esp_err_t atomic_touch_read(atomic_touch_point_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    out->pressed = false;
    out->x = 0;
    out->y = 0;

    if (!s_dev) return ESP_ERR_INVALID_STATE;

    uint8_t buf[7];
    // Read TD_STATUS + P1_XH/XL/YH/YL starting at register 0x02.
    esp_err_t r = read_regs(FT6336G_REG_TD_STATUS, buf, sizeof(buf));
    if (r != ESP_OK) {
        return r;
    }

    uint8_t touches = buf[0] & 0x0F;
    if (touches == 0 || touches > 2) {
        return ESP_OK;
    }

    uint16_t raw_x = ((uint16_t)(buf[1] & 0x0F) << 8) | buf[2];
    uint16_t raw_y = ((uint16_t)(buf[3] & 0x0F) << 8) | buf[4];

    int16_t x, y;
    switch (s_rot) {
    case 90: // 90° CCW: display_x = raw_y, display_y = (native_w - 1) - raw_x
        x = (int16_t)raw_y;
        y = (int16_t)(s_native_w - 1 - raw_x);
        break;
    case 180:
        x = (int16_t)(s_native_w - 1 - raw_x);
        y = (int16_t)(s_native_h - 1 - raw_y);
        break;
    case 270:
        x = (int16_t)(s_native_h - 1 - raw_y);
        y = (int16_t)raw_x;
        break;
    default:
        x = (int16_t)raw_x;
        y = (int16_t)raw_y;
        break;
    }

    out->x = x;
    out->y = y;
    out->pressed = true;
    return ESP_OK;
}

i2c_master_bus_handle_t atomic_touch_get_bus(void) {
    return s_bus;
}
