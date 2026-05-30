// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// atomic_net — minimum-viable network bring-up: netif, wifi, optional NTP.
// Everything mode-specific lives in atomic_wifi; NTP lives in atomic_ntp.
// MQTT and HA have been removed from this copy.

#pragma once

#include "atomic_wifi.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// One-shot: esp_netif_init() + a_wifi_init(cfg). If BIT_NTP_ENABLE is set,
// spawns a small task that calls a_ntp_init() once BIT_WIFI_READY fires.
esp_err_t a_net_init(const a_wifi_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
