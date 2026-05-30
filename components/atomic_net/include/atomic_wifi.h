// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// atomic_wifi — STA and SoftAP, runtime-switchable from the UI.
//
// Compile-time gating (see Kconfig.projbuild for atomic_net):
//   CONFIG_A_WIFI_ENABLE_STA  → compile STA support (default y)
//   CONFIG_A_WIFI_ENABLE_AP   → compile AP support  (default y)
//
// The full public API is declared regardless of which sides are compiled —
// calls into a disabled side return ESP_ERR_NOT_SUPPORTED at runtime so the
// UI links cleanly in any configuration.

#pragma once

#include "esp_err.h"
#include "esp_wifi_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define A_WIFI_SSID_MAX 33 // 32 + NUL
#define A_WIFI_PASS_MAX 65 // 64 + NUL

typedef enum {
    A_WIFI_MODE_STA = 0,
    A_WIFI_MODE_AP = 1,
} a_wifi_mode_t;

typedef struct {
    char ssid[A_WIFI_SSID_MAX];
    int8_t rssi;
    wifi_auth_mode_t authmode;
} a_wifi_ap_t;

// First-boot defaults. Each is consulted only when the corresponding NVS key
// is missing; subsequent boots read directly from NVS.
typedef struct {
    a_wifi_mode_t default_mode;     // mode if "wifi.mode" not in NVS (use A_WIFI_MODE_AP for new units)
    const char *sta_default_ssid;   // optional STA seed; NULL/empty → leave blank
    const char *sta_default_pass;   // optional STA seed
    const char *ap_ssid;            // NULL → use NVS "unit_name" (DEV_UNIT)
    const char *ap_pass;            // NULL/"" → open AP (no password)
} a_wifi_cfg_t;

// Register events, create STA+AP netifs, create wifi driver, start in the
// saved (or default) mode. Idempotent.
esp_err_t a_wifi_init(const a_wifi_cfg_t *cfg);

// ---------------------------------------------------------------------------
//  Mode
// ---------------------------------------------------------------------------

// Persisted mode preference (matches NVS "wifi.mode"). Defaults to AP if NVS
// is empty so new units always come up in setup mode.
a_wifi_mode_t a_wifi_get_mode_pref(void);

// Tear down the current mode, apply `mode`, persist the preference. No-op if
// already in `mode`. ESP_ERR_NOT_SUPPORTED if `mode` isn't compiled in.
esp_err_t a_wifi_apply_mode(a_wifi_mode_t mode);

// ---------------------------------------------------------------------------
//  Credentials
// ---------------------------------------------------------------------------

// Persist STA credentials, then (if currently in STA mode) reconnect to the
// new network. Empty pass → open network.
esp_err_t a_wifi_apply_credentials(const char *ssid, const char *pass);

// Clear STA credentials from NVS. If currently in STA mode, disconnects.
esp_err_t a_wifi_forget(void);

// Load AP credentials (NVS, falling back to defaults from a_wifi_init's cfg).
// ssid_out must hold A_WIFI_SSID_MAX; pass_out A_WIFI_PASS_MAX. Empty pass
// means open.
esp_err_t a_wifi_load_ap_credentials(char *ssid_out, char *pass_out);

// ---------------------------------------------------------------------------
//  Inspection
// ---------------------------------------------------------------------------

// SSID of the active interface (STA: the AP we're associated with; AP: our
// own broadcast SSID). Writes "" if nothing is active.
esp_err_t a_wifi_get_active_ssid(char *out, size_t len);

// URL of the active interface (e.g. "http://192.168.4.1/"). Writes "" if no
// interface has an IP yet.
esp_err_t a_wifi_get_url(char *out, size_t len);

// STA RSSI in dBm. ESP_ERR_INVALID_STATE if not in STA mode or not connected.
esp_err_t a_wifi_get_rssi(int8_t *rssi);

// ---------------------------------------------------------------------------
//  Scan
// ---------------------------------------------------------------------------

// Blocking AP scan. STA-mode only — the UI only exposes Scan from the STA
// pane. Fills up to `max` entries sorted by RSSI desc. Returns count via
// *out_n. ESP_ERR_INVALID_STATE if not in STA mode.
esp_err_t a_wifi_scan(a_wifi_ap_t *aps, uint16_t max, uint16_t *out_n);

#ifdef __cplusplus
}
#endif
