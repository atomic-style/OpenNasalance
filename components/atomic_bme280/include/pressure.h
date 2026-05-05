#pragma once

#include "esp_err.h"

// Experimental BME280 pressure readout (see old/ADDITION.md). Builds an LVGL
// UI in the same frame as the nasometer and starts a sampling task. Disabled
// by default — gate via ENABLE_BME280 in the project's config.h, which routes
// init to pressure_init() instead of nasometer_init().
//
// The Waveshare BME280 breakout shares the FT6336G touch I²C bus on the
// ES3N28P P4 header (SCL=15, SDA=16). The chip lives at 0x76 or 0x77 —
// pressure_init() probes both. Onboard 10k pull-ups; no extra wiring beyond
// VCC/GND/SCL/SDA.

// Sample rate is 25 Hz. Differential pressure shown in Pa relative to a
// baseline captured at startup; y-axis covers PRESSURE_DELTA_MIN_PA ..
// PRESSURE_DELTA_MAX_PA. Avg/Max computed over PRESSURE_STATS_WINDOW_SEC.
// Range matches what the BME280 picks up in open air during speech; widen
// if you couple the sensor port to the nose/mouth via a tube.
#define PRESSURE_SAMPLE_HZ        25
#define PRESSURE_DELTA_MIN_PA     (-10.0f)
#define PRESSURE_DELTA_MAX_PA     (40.0f)
#define PRESSURE_STATS_WINDOW_SEC 10

esp_err_t pressure_init(void);
