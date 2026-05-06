// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "esp_err.h"

#include <stdint.h>

#define A_HTTP_MAX_BINS 256

// unit_name is included in the WS hello message. Pointer must remain valid
// for the lifetime of the server (the component does not copy it).
esp_err_t a_http_init(const char *unit_name);
esp_err_t a_http_stop(void);

// Non-blocking. Last-write-wins: if the broadcast task hasn't drained the
// previous frame yet, this one overwrites it. Safe to call from any task,
// but designed to be called from the audio/DSP producer.
//
// nasalance_pct: 0-100, or 255 for N/A.
void a_http_push_frame(const uint8_t *top_bins,
                       const uint8_t *bot_bins,
                       int n_bins,
                       uint8_t amp_top,
                       uint8_t amp_bot,
                       uint8_t nasalance_pct);
