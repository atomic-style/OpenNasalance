// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "atomic_lcd_es3n28p.h"

#include "atomic_err.h"
#include "atomic_gpio.h"
#include "atomic_lcd_config.h"
#include "atomic_log.h"
#include "driver/spi_common.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "䋧";

// Native panel orientation is portrait 240x320. We rotate at the MADCTL level
// (swap_xy + mirror_x → MV=1, MX=1, MY=0) so higher layers see landscape
// 320x240. The matching touch rotation lives in a_lcd_cfg.touch_rotation_ccw
// just below — keep these two in lockstep.
#define ES3N28P_H_RES 320
#define ES3N28P_V_RES 240
#define ES3N28P_BPP 16
#define ES3N28P_RGB_ELE_ORDER LCD_BGR
#define ES3N28P_MAX_TRANSFER_SZ (ES3N28P_H_RES * 40 * ES3N28P_BPP / 8)
// Display MADCTL: MV=1, MX=1, MY=0  ⇒  touch needs +90° CCW from native.
#define ES3N28P_TOUCH_ROTATION_CCW 90

#define PIN_LCD_CS 10
#define PIN_LCD_DC 46
#define PIN_LCD_SCK 12
#define PIN_LCD_MOSI 11
#define PIN_LCD_MISO 13
#define PIN_LCD_BL 45
#define PIN_LCD_RST (-1)

static const a_lcd_cfg_t a_lcd_cfg = {
    .h_res = ES3N28P_H_RES,
    .v_res = ES3N28P_V_RES,
    .bpp = ES3N28P_BPP,
    .rgb = ES3N28P_RGB_ELE_ORDER,
    .max_transfer_sz = ES3N28P_MAX_TRANSFER_SZ,
    .touch_rotation_ccw = ES3N28P_TOUCH_ROTATION_CCW,
};

// QDtech's factory init sequence for ILI9341V on this module. Translated
// one-to-one from docs/ES3N28P/.../ILI9341V_Init.txt.
static const ili9341_lcd_init_cmd_t qd_ili9341v_init[] = {
    {0xCF, (uint8_t[]){0x00, 0xC1, 0x30}, 3, 0},
    {0xED, (uint8_t[]){0x64, 0x03, 0x12, 0x81}, 4, 0},
    {0xE8, (uint8_t[]){0x85, 0x00, 0x78}, 3, 0},
    {0xCB, (uint8_t[]){0x39, 0x2C, 0x00, 0x34, 0x02}, 5, 0},
    {0xF7, (uint8_t[]){0x20}, 1, 0},
    {0xEA, (uint8_t[]){0x00, 0x00}, 2, 0},
    {0xC0, (uint8_t[]){0x13}, 1, 0},
    {0xC1, (uint8_t[]){0x13}, 1, 0},
    {0xC5, (uint8_t[]){0x22, 0x35}, 2, 0},
    {0xC7, (uint8_t[]){0xBD}, 1, 0},
    // 0x21: display inversion ON (IPS panels expect this)
    {0x21, NULL, 0, 0},
    {0xB6, (uint8_t[]){0x0A, 0xA2}, 2, 0},
    {0xF6, (uint8_t[]){0x01, 0x30}, 2, 0},
    {0xB1, (uint8_t[]){0x00, 0x1B}, 2, 0},
    {0xF2, (uint8_t[]){0x00}, 1, 0},
    {0x26, (uint8_t[]){0x01}, 1, 0},
    {0xE0,
     (uint8_t[]){0x0F, 0x35, 0x31, 0x0B, 0x0E, 0x06, 0x49, 0xA7, 0x33, 0x07,
                 0x0F, 0x03, 0x0C, 0x0A, 0x00},
     15, 0},
    {0xE1,
     (uint8_t[]){0x00, 0x0A, 0x0F, 0x04, 0x11, 0x08, 0x36, 0x58, 0x4D, 0x07,
                 0x10, 0x0C, 0x32, 0x34, 0x0F},
     15, 0},
};

static const ili9341_vendor_config_t qd_vendor_cfg = {
    .init_cmds = qd_ili9341v_init,
    .init_cmds_size = sizeof(qd_ili9341v_init) / sizeof(qd_ili9341v_init[0]),
};

static const spi_bus_config_t a_spi_bus_cfg = {
    .sclk_io_num = PIN_LCD_SCK,
    .mosi_io_num = PIN_LCD_MOSI,
    .miso_io_num = PIN_LCD_MISO,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .data4_io_num = -1,
    .data5_io_num = -1,
    .data6_io_num = -1,
    .data7_io_num = -1,
    .max_transfer_sz = (uint32_t)ES3N28P_MAX_TRANSFER_SZ,
};

static esp_lcd_panel_io_spi_config_t a_spi_io_cfg = {
    .cs_gpio_num = PIN_LCD_CS,
    .dc_gpio_num = PIN_LCD_DC,
    .spi_mode = 0,
    .pclk_hz = 40 * 1000 * 1000,
    .trans_queue_depth = 10,
    .on_color_trans_done = NULL,
    .user_ctx = NULL,
    .lcd_cmd_bits = 8,
    .lcd_param_bits = 8,
    .flags = {.quad_mode = false},
};

static esp_lcd_panel_dev_config_t a_dev_cfg = {
    .reset_gpio_num = PIN_LCD_RST,
    .rgb_ele_order = ES3N28P_RGB_ELE_ORDER,
    .bits_per_pixel = 16,
    .vendor_config = (void *)&qd_vendor_cfg,
};

esp_err_t a_lcd_es3n28p(a_lcd_t *lcd,
                        bool (*cb)(esp_lcd_panel_io_handle_t,
                                   esp_lcd_panel_io_event_data_t *, void *)) {
  info(TAG, "a_lcd_es3n28p()");
  esp_lcd_panel_handle_t panel = NULL;
  esp_lcd_panel_io_handle_t io_handle = NULL;

  info(TAG, "spi bus init host=%d", LCD_SPI_HOST);
  try(spi_bus_initialize(LCD_SPI_HOST, &a_spi_bus_cfg, SPI_DMA_CH_AUTO));

  a_spi_io_cfg.on_color_trans_done = cb;
  try(esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &a_spi_io_cfg, &io_handle));
  try(esp_lcd_new_panel_ili9341(io_handle, &a_dev_cfg, &panel));

  try(esp_lcd_panel_reset(panel));
  try(esp_lcd_panel_init(panel));
  // 90° CW rotation (180° flipped from 90° CCW): MADCTL MV=1, MX=1, MY=0
  try(esp_lcd_panel_swap_xy(panel, true));
  try(esp_lcd_panel_mirror(panel, true, false));
  try(esp_lcd_panel_disp_on_off(panel, true));

  atomic_gpio_config_out(PIN_LCD_BL, false, false);
  atomic_gpio_set(PIN_LCD_BL, 1);

  lcd->id = LCD_ES3N28P;
  lcd->panel = panel;
  lcd->cfg = &a_lcd_cfg;
  lcd->io_handle = io_handle;

  info(TAG, "es3n28p up: %dx%d", ES3N28P_H_RES, ES3N28P_V_RES);
  return ESP_OK;
}
