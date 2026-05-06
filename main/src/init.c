// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "init.h"
#include "init_display.h"
#include "init_touch.h"
#include "a_evt.h"
#include "a_evt_user.h"
#include "atomic_bits.h"
#include "atomic_nvs.h"
#include "atomic_err.h"
#include "atomic_log.h"
#include "atomic_ui.h"
#include "config.h"
#include "esp_err.h"
#include "a_mic.h"
#include "nasometer.h"

#ifdef ENABLE_BME280
#include "pressure.h"
#endif

#ifdef ENABLE_SD
#include "atomic_sd.h"
#endif

#ifdef ENABLE_WIFI
#include <string.h>
#include "atomic_net.h"
#include "wifi_credentials.h"
#endif

#ifdef ENABLE_HTTP
#include "atomic_http.h"
#endif

static const char *TAG = "、";

static esp_err_t init_nvs_defaults(void) {
    uint8_t device_id = (uint8_t)DEV_ID;
    char device_chip_id[16] = {'\0'};
    char device_unit_name[16] = {'\0'};
    strcpy(device_chip_id, DEV_CHIP_ID);
    strcpy(device_unit_name, DEV_UNIT);
    try(a_nvs_set_u8("device_id", device_id));
    try(a_nvs_set_str("chip_id", device_chip_id));
    try(a_nvs_set_str("unit_name", device_unit_name));
    return ESP_OK;
}

static esp_err_t init_nvs(void) {
    try(a_nvs_init());
    nvs_type_t device_id_type;
    esp_err_t ok = a_nvs_find_key("device_id", &device_id_type);
    if (ok != ESP_OK)
        try(init_nvs_defaults());
    return ESP_OK;
}

#ifdef ENABLE_WIFI
static esp_err_t init_net(void) {
    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, WIFI_SSID, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, WIFI_PASSWORD, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    a_bits_set(BIT_NTP_ENABLE); // a_net_task brings up NTP after wifi associates
    return a_net_init(&cfg);
}
#endif

#ifdef ENABLE_SD
static esp_err_t init_sd(void) {
#ifdef SDMMC
    a_sdmmc_cfg_t cfg = {
        .width = SDMMC_SLOT_WIDTH,
        .clk_io_num = PIN_SDMMC_CLK,
        .cmd_io_num = PIN_SDMMC_CMD,
        .data_io_num = PIN_SDMMC_DATA0,
        .data_1_io_num = PIN_SDMMC_DATA1,
        .data_2_io_num = PIN_SDMMC_DATA2,
        .data_3_io_num = PIN_SDMMC_DATA3,
    };
    return atomic_sdmmc_init(&cfg);
#elif defined(SDSPI)
    a_sdspi_cfg_t cfg = {
        .host = SDSPI_HOST,
        .mosi_io_num = PIN_SDSPI_MOSI,
        .miso_io_num = PIN_SDSPI_MISO,
        .sclk_io_num = PIN_SDSPI_SCLK,
        .cs_io_num = PIN_SDSPI_CS,
    };
    return atomic_sdspi_init(&cfg);
#else
    warn(TAG, "ENABLE_SD set but no SDMMC/SDSPI pin block for this board");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
#endif // ENABLE_SD

void init() {
    notice(TAG, "init()");
    try(init_nvs());
    try(a_bits_init());
    try(a_evt_init());

#ifdef ENABLE_DISPLAY
    try(init_display());
    try(atomic_ui());
#endif // ENABLE_DISPLAY

#ifdef ENABLE_TOUCH
    try(init_touch());
#endif // ENABLE_TOUCH

#ifdef ENABLE_WIFI
    try(init_net());
#endif // ENABLE_WIFI

#ifdef ENABLE_HTTP
    try(a_http_init(DEV_UNIT));
#endif // ENABLE_HTTP

#ifdef ENABLE_SD
    // Mount SD before display so atomic_lvgl's task picks up BIT_SD_READY
    // before registering its filesystem driver.
    if (init_sd() != ESP_OK) {
        warn(TAG, "SD mount failed — continuing without storage");
    }
#endif // ENABLE_SD

    try(a_evt_user_init());

#ifdef ENABLE_BME280
    // Pressure-only mode: skip mic init and replace the nasometer UI with a
    // rolling BME280 trace. See ADDITION.md.
    try(pressure_init());
#endif // ENABLE_BME280

#ifdef ENABLE_MIC
    a_mic_441_config_t a_mic_441_config = {
        .pin_clk = PIN_MIC_CLK,
        .pin_ws = PIN_MIC_WS,
        .pin_data_1 = PIN_MIC_DATA_1,
        .pin_data_2 = PIN_MIC_DATA_2,
        .sample_rate = MIC_SAMPLE_RATE,
    };
    try(a_mic_441_init(&a_mic_441_config));
    try(nasometer_init());
#endif // ENABLE_MIC

    try(a_ui_clear());
    info(TAG, "init() done.");
}