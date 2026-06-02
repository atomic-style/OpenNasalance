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
#include <string.h> // strcpy in init_nvs_defaults

#ifdef ENABLE_BME280
#include "pressure.h"
#endif

#ifdef ENABLE_SD
#include "atomic_sd.h"
#endif

#ifdef ENABLE_WIFI
#include "atomic_net.h"
// wifi_credentials.h is optional — if the file isn't present (e.g. clean
// clone, no STA seed wanted), STA simply starts blank and the user enters
// credentials via the on-screen Wi-Fi menu.
#if __has_include("wifi_credentials.h")
#include "wifi_credentials.h"
#define HAVE_WIFI_CREDENTIALS 1
#endif
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
    a_wifi_cfg_t cfg = {
        .default_mode = WIFI_DEFAULT_MODE,
#ifdef HAVE_WIFI_CREDENTIALS
        .sta_default_ssid = WIFI_SSID,
        .sta_default_pass = WIFI_PASSWORD,
#endif
        .ap_ssid = WIFI_AP_SSID, // NULL → atomic_wifi uses NVS "unit_name" (DEV_UNIT)
        .ap_pass = WIFI_AP_PASS,
    };
    a_bits_set(BIT_NTP_ENABLE); // atomic_net spawns NTP once STA associates
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
    try(atomic_ui(DEV_PROJECT));
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
    // SD is mounted here, after the display/UI are up. atomic_lvgl's task
    // tolerates BIT_SD_READY arriving after it starts (it gates JPEG service on
    // the queue handle, not the bit). WAV recording uses fopen("/sd/...")
    // directly, independent of LVGL's filesystem driver.
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
    // Mic-init failure is non-fatal: the nasometer view still needs to come
    // up so the user has UI even when the mic hardware isn't present.
#if defined(MIC_T5837)
    a_mic_t5837_config_t a_mic_cfg = {
        .pin_clk = PIN_MIC_CLK,
        .pin_data_1 = PIN_MIC_DATA_1,
        .pin_data_2 = PIN_MIC_DATA_2,
        .sample_rate = MIC_SAMPLE_RATE,
        .buf_samples = MIC_BUF_SAMPLES,
        .gain = MIC_SW_GAIN,
#ifdef MIC_T5837_STEREO
        .stereo = true,
#else
        .stereo = false,
#endif
    };
    esp_err_t mic_r = a_mic_t5837_init(&a_mic_cfg);
#elif defined(MIC_441)
    a_mic_441_config_t a_mic_cfg = {
        .pin_clk = PIN_MIC_CLK,
        .pin_ws = PIN_MIC_WS,
        .pin_data_1 = PIN_MIC_DATA_1,
        .pin_data_2 = PIN_MIC_DATA_2,
        .sample_rate = MIC_SAMPLE_RATE,
    };
    esp_err_t mic_r = a_mic_441_init(&a_mic_cfg);
#endif
    if (mic_r != ESP_OK) {
        warn(TAG, "mic init failed: %s — nasometer will show no data", esp_err_to_name(mic_r));
    }
#endif // ENABLE_MIC

#ifdef ENABLE_ATOMIC_MIC

    a_mic_config_t a_mic_cfg = {
        .pin_clk = PIN_MIC_CLK,
        .pin_data = PIN_MIC_DATA,
        .format = I2S_PDM_DATA_FMT_PCM, // I2S_PDM_DATA_FMT_RAW,
        .slot = I2S_PDM_SLOT_BOTH,
    };
    ESP_LOGI(TAG, "init calling atomic_mic_init()");
    esp_err_t ok = a_mic_init(&a_mic_cfg);
    if (ok != ESP_OK) {
        ESP_LOGE(TAG, "atomic_mic_init() error %s", esp_err_to_name(ok));
    }
#endif

#ifdef ENABLE_NASOMETER
    try(nasometer_init());
    try(a_ui_clear());
#endif

    info(TAG, "init() done.");
}