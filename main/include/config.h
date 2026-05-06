// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

typedef enum {
    W5 = 0,
    CYD = 1,
    TDISPLAY = 2,
    WS_TOUCH_LCD = 3,
    S3_MINI = 4,
    N8R8 = 5,
    P4 = 6,
    RING = 7,
    ES3N28P = 8
} α_dev_id_t;

// =============================================================================
//  OpenNasalance — build-time configuration
// =============================================================================
//  All compile-time options for the firmware live in this single file.
//  See README.md for a higher-level overview.
//
//  Layout:
//    1. Hardware target          — pick one ESP_<BOARD> #define
//    2. Feature toggles          — enable optional subsystems
//    3. Device identity          — strings the firmware reports about itself
//    4. Hardware pins            — per-board pin map (only the active board's
//                                   block is compiled)
//    5. Microphone & spectrograph — DSP/UI knobs (board-independent)
//
//  The atomic_bme280 component reads its own constants from
//  components/atomic_bme280/include/pressure.h — not this file — so the
//  experiment can be removed without touching this header.
// =============================================================================

// -----------------------------------------------------------------------------
// 1. Hardware target — uncomment exactly one
// -----------------------------------------------------------------------------
#define ESP_ES3N28P // QDtech 2.8" ES3N28P (ESP32-S3R8, 16MB flash, 8MB PSRAM)

// -----------------------------------------------------------------------------
// 2. Feature toggles
// -----------------------------------------------------------------------------
#define ENABLE_LCD   // 320×240 IPS panel via atomic_lcd
#define ENABLE_LVGL  // LVGL UI on top of ENABLE_LCD
#define ENABLE_TOUCH // FT6336G capacitive touch
// #define ENABLE_SD    // SDMMC slot for WAV recording
#define ENABLE_MIC  // Dual MEMS-441 (TDM I²S)
#define ENABLE_WIFI // Wi-Fi STA + NTP (requires private/wifi_credentials.h)
#define ENABLE_HTTP // HTTP server on port 80 (requires ENABLE_WIFI)

// Experimental: replaces the nasometer UI with a rolling BME280 pressure
// trace (see components/atomic_bme280, old/ADDITION.md). Off by default.
// #define ENABLE_BME280

// init.c gates display init on this — set automatically by ENABLE_LCD.
#ifdef ENABLE_LCD
#define ENABLE_DISPLAY
#endif

// -----------------------------------------------------------------------------
// 3. Device identity — surfaced via NVS at first boot
// -----------------------------------------------------------------------------

#ifdef ESP_ES3N28P
#define DEV_ID ES3N28P
#define DEV_UNIT "ES3N28P_01"
#define DEV_TARGET "ESP32S3"
#define DEV_CHIP_ID "ESP32-S3R8"
#define DEV_NAME "QDtech 2.8in ES3N28P"
#define DEV_MODEL "ES3N28P"
#define DEV_LOC "DEV"
#endif

// -----------------------------------------------------------------------------
// 4. Hardware pins — ES3N28P
// -----------------------------------------------------------------------------
#ifdef ESP_ES3N28P

// SD slot — wired to SDMMC slot 1 in 4-bit mode (per QDtech demo Show_SD_Jpg.ino).
#ifdef ENABLE_SD
#define SDMMC
#define SDMMC_SLOT_WIDTH 4
#define SDSPI_HOST SPI2_HOST // unused on this board (SDMMC), kept for API symmetry
#define PIN_SDMMC_CLK 38
#define PIN_SDMMC_CMD 40
#define PIN_SDMMC_DATA0 39
#define PIN_SDMMC_DATA1 41
#define PIN_SDMMC_DATA2 48
#define PIN_SDMMC_DATA3 47
#endif // ENABLE_SD

// I²S microphones — exposed on the P3 expansion header (GPIO 2/3/14/21).
// GPIO 3 is a strapping pin; safe as input after boot, but never hold low at reset.
#ifdef ENABLE_MIC
#define PIN_MIC_CLK 14
#define PIN_MIC_WS 21
#define PIN_MIC_DATA_1 2
#define PIN_MIC_DATA_2 3
#endif // ENABLE_MIC

// FT6336G capacitive touch — shared I²C bus on the P4 expansion header.
// Reset/INT are broken out on the capacitive-touch SKU only.
#ifdef ENABLE_TOUCH
#define TOUCH_I2C_PORT 0
#define PIN_TOUCH_SCL 15
#define PIN_TOUCH_SDA 16
#define PIN_TOUCH_RST 18
#define PIN_TOUCH_INT 17
// Native portrait dimensions as the FT6336G reports them.
#define TOUCH_NATIVE_W 240
#define TOUCH_NATIVE_H 320
// Display uses MADCTL swap_xy + mirror_y; the FT6336G's native axes come out
// 180° from that, so applying 270° CCW to the raw touch matches what the eye
// sees on screen.
#define TOUCH_ROTATION_CCW 270
#endif // ENABLE_TOUCH

#endif // ESP_ES3N28P

// -----------------------------------------------------------------------------
// 5. Microphone & spectrograph — board-independent
// -----------------------------------------------------------------------------
#ifdef ENABLE_MIC

#define MIC_SAMPLE_RATE 24000
#define MIC_BUF_SAMPLES 256

// Spectrograph (Nasometer UI).
// At 24 kHz with hop=512 each column represents ~21.3 ms. With 320 columns
// (landscape width of the panel) the rolling window is ~6.8 s. Set
// SPECTRO_HOP to 384 for a ~5.12 s window.
#define SPECTRO_FFT_SIZE 512
#define SPECTRO_HOP 512
#define SPECTRO_MIN_HZ 0
#define SPECTRO_MAX_HZ 6000
#define SPECTRO_DB_FLOOR (-70)
#define SPECTRO_DB_CEIL (-10)

// Nasalance: 100 * E_nasal / (E_nasal + E_oral) over a narrow passband.
// The Kay Nasometer convention is a ~300 Hz wide filter centered around 500 Hz;
// the 60..2000 Hz default below is wider/conservative. Cross-check against
// docs/nasometer/Louisiana State University nasalance protocol standardization.pdf
// before any clinical interpretation.
#define NASALANCE_BAND_LOW_HZ 60
#define NASALANCE_BAND_HIGH_HZ 2000
#define NASALANCE_WINDOW_SEC 10
// Mic assignment: 1 = nasal mic on data_1, 2 = nasal mic on data_2.
#define NASALANCE_NASAL_MIC 1

#endif // ENABLE_MIC
