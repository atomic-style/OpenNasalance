// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// atomic_wifi — STA + SoftAP with runtime mode switching.
//
// State machine (single source of truth: s_mode):
//   init():
//     - create STA + AP netifs (both, regardless of compiled support — cheap)
//     - register WIFI_EVENT + IP_EVENT handlers
//     - esp_wifi_init() once
//     - on first boot: persist cfg->default_mode to NVS; seed STA creds from
//       cfg->sta_default_* if NVS keys missing
//     - call apply_mode_internal(saved_pref)
//   apply_mode(new):
//     - esp_wifi_stop()
//     - esp_wifi_set_mode(STA or AP)
//     - load and set the per-mode wifi_config_t
//     - esp_wifi_start()  → triggers STA_START or AP_START event
//     - persist NVS "wifi.mode"
//
// Bit ownership (atomic_bits):
//   BIT_WIFI_START     set on STA_START
//   BIT_WIFI_WAIT      set while a connect attempt is outstanding
//   BIT_WIFI_READY     set strictly on STA_GOT_IP, cleared on STA_DISCONNECTED.
//                      Used by NTP (it only makes sense when we have an
//                      uplink); NOT set in AP mode.
//   BIT_WIFI_AP_READY  set on AP_START, cleared on AP_STOP. atomic_http waits
//                      on this OR BIT_WIFI_READY, so the server still starts
//                      in either mode.

#include "atomic_wifi.h"

#include "atomic_bits.h"
#include "atomic_log.h"
#include "atomic_nvs.h"
#include "atomic_wifi_evt.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "ㄱ";

// ---------------------------------------------------------------------------
//  Compile-time gating helpers
// ---------------------------------------------------------------------------

// If atomic_net's Kconfig hasn't been applied (older sdkconfig that predates
// this refactor), default both sides ON to preserve build-ability.
#ifndef CONFIG_A_WIFI_ENABLE_STA
#define CONFIG_A_WIFI_ENABLE_STA 1
#endif
#ifndef CONFIG_A_WIFI_ENABLE_AP
#define CONFIG_A_WIFI_ENABLE_AP 1
#endif

#if !CONFIG_A_WIFI_ENABLE_STA && !CONFIG_A_WIFI_ENABLE_AP
#error "atomic_wifi: both STA and AP disabled — nothing to build. Enable at least one."
#endif

// ---------------------------------------------------------------------------
//  NVS keys
// ---------------------------------------------------------------------------
#define NVS_KEY_MODE     "wifi.mode"
#define NVS_KEY_STA_SSID "wifi.sta.ssid"
#define NVS_KEY_STA_PASS "wifi.sta.pass"
#define NVS_KEY_AP_SSID  "wifi.ap.ssid"
#define NVS_KEY_AP_PASS  "wifi.ap.pass"

// ---------------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------------

static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;

static a_wifi_mode_t s_mode = A_WIFI_MODE_AP; // active mode (after a_wifi_init)
static bool s_inited = false;

// Captured AP defaults from the cfg passed to a_wifi_init, used when NVS has
// no AP creds. Pointers passed in cfg must remain valid; we copy here to
// detach lifetime.
static char s_def_ap_ssid[A_WIFI_SSID_MAX] = {0};
static char s_def_ap_pass[A_WIFI_PASS_MAX] = {0};

// Serializes mode/credential changes (UI is single-threaded but defence in
// depth is cheap and keeps the event handler off contended state).
static SemaphoreHandle_t s_mu = NULL;

// TX-power scaling (STA only). Lifted verbatim from atomic_net_bad — backs
// off after disconnects, restores after a successful association.
#if CONFIG_A_WIFI_ENABLE_STA
static const int8_t WIFI_POWER_LEVEL[11] = {8, 20, 28, 34, 44, 52, 56, 60, 66, 72, 80};
static int8_t s_power_level = 10;

static esp_err_t set_tx_power(void) {
    esp_err_t ok = esp_wifi_set_max_tx_power(WIFI_POWER_LEVEL[s_power_level]);
    if (ok != ESP_OK) {
        warn(TAG, "esp_wifi_set_max_tx_power(%d) failed: %s", s_power_level, esp_err_to_name(ok));
    }
    return ok;
}
#endif

// ---------------------------------------------------------------------------
//  NVS helpers
// ---------------------------------------------------------------------------

static esp_err_t nvs_load_str(const char *key, char *out, size_t cap) {
    size_t len = cap;
    esp_err_t r = a_nvs_get_str(key, out, &len);
    if (r != ESP_OK) out[0] = '\0';
    return r;
}

// Load STA credentials from NVS into a wifi_sta_config_t. Returns true if both
// SSID and (possibly empty) record exist; sets ssid[0]=0 if the key is missing.
static bool load_sta_creds(wifi_sta_config_t *out) {
    char ssid[A_WIFI_SSID_MAX] = {0};
    char pass[A_WIFI_PASS_MAX] = {0};
    nvs_load_str(NVS_KEY_STA_SSID, ssid, sizeof ssid);
    nvs_load_str(NVS_KEY_STA_PASS, pass, sizeof pass);
    memset(out, 0, sizeof *out);
    strncpy((char *)out->ssid, ssid, sizeof out->ssid);
    strncpy((char *)out->password, pass, sizeof out->password);
    out->threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    return ssid[0] != '\0';
}

// Load AP credentials into a wifi_ap_config_t. Always succeeds — falls back
// to the defaults captured at init() if NVS keys are missing.
static void load_ap_creds(wifi_ap_config_t *out) {
    char ssid[A_WIFI_SSID_MAX] = {0};
    char pass[A_WIFI_PASS_MAX] = {0};
    nvs_load_str(NVS_KEY_AP_SSID, ssid, sizeof ssid);
    nvs_load_str(NVS_KEY_AP_PASS, pass, sizeof pass);
    if (!ssid[0]) strncpy(ssid, s_def_ap_ssid, sizeof ssid - 1);
    if (!pass[0]) strncpy(pass, s_def_ap_pass, sizeof pass - 1);

    memset(out, 0, sizeof *out);
    strncpy((char *)out->ssid, ssid, sizeof out->ssid);
    out->ssid_len = (uint8_t)strlen(ssid);
    strncpy((char *)out->password, pass, sizeof out->password);
    out->channel = 1;
    out->authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    out->max_connection = 4;
    out->beacon_interval = 100;
    // WPA2 requires >=8-char password. Force OPEN if the user set something shorter.
    if (pass[0] && strlen(pass) < 8) {
        out->authmode = WIFI_AUTH_OPEN;
        out->password[0] = '\0';
        warn(TAG, "AP password < 8 chars — using OPEN");
    }
}

// ---------------------------------------------------------------------------
//  Event handlers
// ---------------------------------------------------------------------------

static void wifi_event_handler(void *handler_arg, esp_event_base_t evt_base, int32_t evt_id,
                               void *evt_data) {
    notice(TAG, "wifi event %ld: %s", (long)evt_id, a_wifi_evt_name((wifi_event_t)evt_id));
    switch (evt_id) {
#if CONFIG_A_WIFI_ENABLE_STA
    case WIFI_EVENT_STA_START:
        a_bits_set(BIT_WIFI_START);
        a_bits_set(BIT_WIFI_WAIT);
        esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_CONNECTED: {
        wifi_event_sta_connected_t *c = (wifi_event_sta_connected_t *)evt_data;
        info(TAG, "STA connected SSID=\"%.*s\" channel=%u bssid=" MACSTR, c->ssid_len, c->ssid,
             c->channel, MAC2STR(c->bssid));
        // Successful association → restore power level next time.
        s_power_level = 10;
        break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: {
        a_bits_clear(BIT_WIFI_READY);
        a_bits_clear(BIT_WIFI_WAIT);
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)evt_data;
        warn(TAG, "STA disconnect: %d (%s)", d->reason, a_wifi_err_reason(d->reason));
        if (s_mode != A_WIFI_MODE_STA) break; // user switched away — don't reconnect
        // Reconnect only if we have creds; otherwise stay disconnected until
        // a_wifi_apply_credentials gives us a network.
        wifi_config_t cur = {0};
        esp_wifi_get_config(WIFI_IF_STA, &cur);
        if (cur.sta.ssid[0]) {
            // Back off TX power a notch (carry-over from atomic_net_bad).
            if (s_power_level > 0) {
                s_power_level--;
            } else {
                s_power_level = 10;
            }
            set_tx_power();
            a_bits_set(BIT_WIFI_WAIT);
            esp_wifi_connect();
        }
        break;
    }
#endif
#if CONFIG_A_WIFI_ENABLE_AP
    case WIFI_EVENT_AP_START:
        info(TAG, "AP started");
        a_bits_set(BIT_WIFI_AP_READY);
        // Intentionally NOT setting BIT_WIFI_READY here: that bit is the
        // STA-has-IP signal and gates NTP, which can't reach an upstream
        // time server while we're acting as an AP. atomic_http waits on
        // (BIT_WIFI_READY | BIT_WIFI_AP_READY), so the server still starts.
        break;
    case WIFI_EVENT_AP_STOP:
        info(TAG, "AP stopped");
        a_bits_clear(BIT_WIFI_AP_READY);
        break;
    case WIFI_EVENT_AP_STACONNECTED: {
        wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)evt_data;
        info(TAG, "AP client joined " MACSTR " aid=%u", MAC2STR(e->mac), e->aid);
        break;
    }
    case WIFI_EVENT_AP_STADISCONNECTED: {
        wifi_event_ap_stadisconnected_t *e = (wifi_event_ap_stadisconnected_t *)evt_data;
        info(TAG, "AP client left " MACSTR " aid=%u", MAC2STR(e->mac), e->aid);
        break;
    }
#endif
    default:
        break;
    }
}

static void ip_event_handler(void *handler_arg, esp_event_base_t evt_base, int32_t evt_id,
                             void *evt_data) {
    notice(TAG, "ip event %ld: %s", (long)evt_id, a_ip_evt_name((ip_event_t)evt_id));
    switch (evt_id) {
#if CONFIG_A_WIFI_ENABLE_STA
    case IP_EVENT_STA_GOT_IP: {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)evt_data;
        notice(TAG, "STA got ip: " IPSTR, IP2STR(&e->ip_info.ip));
        a_bits_set(BIT_WIFI_READY);
        a_bits_clear(BIT_WIFI_WAIT);
        a_nvs_set_u8("wifi.pwr.last", (uint8_t)s_power_level);
        break;
    }
#endif
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
//  Mode application
// ---------------------------------------------------------------------------

#if CONFIG_A_WIFI_ENABLE_STA
static esp_err_t apply_sta(void) {
    wifi_config_t cfg = {0};
    load_sta_creds(&cfg.sta);
    esp_err_t r = esp_wifi_set_mode(WIFI_MODE_STA);
    if (r != ESP_OK) return r;
    r = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (r != ESP_OK) return r;
    set_tx_power();
    return esp_wifi_start();
}
#endif

#if CONFIG_A_WIFI_ENABLE_AP
static esp_err_t apply_ap(void) {
    wifi_config_t cfg = {0};
    load_ap_creds(&cfg.ap);
    esp_err_t r = esp_wifi_set_mode(WIFI_MODE_AP);
    if (r != ESP_OK) return r;
    r = esp_wifi_set_config(WIFI_IF_AP, &cfg);
    if (r != ESP_OK) return r;
    return esp_wifi_start();
}
#endif

// Caller holds s_mu. Stops the driver, applies the new mode, persists pref.
static esp_err_t apply_mode_internal(a_wifi_mode_t mode) {
    // esp_wifi_stop() is a no-op if not started.
    esp_wifi_stop();
    a_bits_clear(BIT_WIFI_START);
    a_bits_clear(BIT_WIFI_WAIT);
    a_bits_clear(BIT_WIFI_READY);
    a_bits_clear(BIT_WIFI_AP_READY);

    esp_err_t r = ESP_ERR_NOT_SUPPORTED;
    if (mode == A_WIFI_MODE_STA) {
#if CONFIG_A_WIFI_ENABLE_STA
        r = apply_sta();
#endif
    } else {
#if CONFIG_A_WIFI_ENABLE_AP
        r = apply_ap();
#endif
    }
    if (r != ESP_OK) {
        err(TAG, "apply_mode(%d) failed: %s", (int)mode, esp_err_to_name(r));
        return r;
    }
    s_mode = mode;
    a_nvs_set_u8(NVS_KEY_MODE, (uint8_t)mode);
    info(TAG, "mode → %s", mode == A_WIFI_MODE_AP ? "AP" : "STA");
    return ESP_OK;
}

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------

a_wifi_mode_t a_wifi_get_mode_pref(void) {
    uint8_t v = (uint8_t)A_WIFI_MODE_AP;
    a_nvs_get_u8(NVS_KEY_MODE, &v);
    if (v != A_WIFI_MODE_AP && v != A_WIFI_MODE_STA) v = (uint8_t)A_WIFI_MODE_AP;
#if !CONFIG_A_WIFI_ENABLE_AP
    if (v == A_WIFI_MODE_AP) v = (uint8_t)A_WIFI_MODE_STA;
#endif
#if !CONFIG_A_WIFI_ENABLE_STA
    if (v == A_WIFI_MODE_STA) v = (uint8_t)A_WIFI_MODE_AP;
#endif
    return (a_wifi_mode_t)v;
}

esp_err_t a_wifi_apply_mode(a_wifi_mode_t mode) {
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (mode == A_WIFI_MODE_AP) {
#if !CONFIG_A_WIFI_ENABLE_AP
        return ESP_ERR_NOT_SUPPORTED;
#endif
    } else {
#if !CONFIG_A_WIFI_ENABLE_STA
        return ESP_ERR_NOT_SUPPORTED;
#endif
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
    esp_err_t r = (mode == s_mode) ? ESP_OK : apply_mode_internal(mode);
    xSemaphoreGive(s_mu);
    return r;
}

esp_err_t a_wifi_apply_credentials(const char *ssid, const char *pass) {
#if !CONFIG_A_WIFI_ENABLE_STA
    (void)ssid;
    (void)pass;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!ssid || !ssid[0]) return ESP_ERR_INVALID_ARG;
    if (!pass) pass = "";
    if (strlen(ssid) >= A_WIFI_SSID_MAX) return ESP_ERR_INVALID_ARG;
    if (strlen(pass) >= A_WIFI_PASS_MAX) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_mu, portMAX_DELAY);
    a_nvs_set_str(NVS_KEY_STA_SSID, ssid);
    a_nvs_set_str(NVS_KEY_STA_PASS, pass);
    info(TAG, "STA creds saved: ssid=%s pass=%s", ssid, pass[0] ? "(set)" : "(open)");

    esp_err_t r = ESP_OK;
    if (s_mode == A_WIFI_MODE_STA) {
        wifi_config_t cfg = {0};
        load_sta_creds(&cfg.sta);
        esp_wifi_disconnect();
        r = esp_wifi_set_config(WIFI_IF_STA, &cfg);
        if (r == ESP_OK) {
            s_power_level = 10;
            set_tx_power();
            a_bits_set(BIT_WIFI_WAIT);
            r = esp_wifi_connect();
        }
    }
    xSemaphoreGive(s_mu);
    return r;
#endif
}

esp_err_t a_wifi_forget(void) {
#if !CONFIG_A_WIFI_ENABLE_STA
    return ESP_ERR_NOT_SUPPORTED;
#else
    xSemaphoreTake(s_mu, portMAX_DELAY);
    a_nvs_erase_key(NVS_KEY_STA_SSID);
    a_nvs_erase_key(NVS_KEY_STA_PASS);
    if (s_mode == A_WIFI_MODE_STA) {
        esp_wifi_disconnect();
        wifi_config_t cfg = {0};
        esp_wifi_set_config(WIFI_IF_STA, &cfg);
        a_bits_clear(BIT_WIFI_READY);
        a_bits_clear(BIT_WIFI_WAIT);
    }
    xSemaphoreGive(s_mu);
    info(TAG, "STA credentials forgotten");
    return ESP_OK;
#endif
}

esp_err_t a_wifi_load_ap_credentials(char *ssid_out, char *pass_out) {
    if (!ssid_out || !pass_out) return ESP_ERR_INVALID_ARG;
    nvs_load_str(NVS_KEY_AP_SSID, ssid_out, A_WIFI_SSID_MAX);
    nvs_load_str(NVS_KEY_AP_PASS, pass_out, A_WIFI_PASS_MAX);
    if (!ssid_out[0]) strncpy(ssid_out, s_def_ap_ssid, A_WIFI_SSID_MAX - 1);
    if (!pass_out[0]) strncpy(pass_out, s_def_ap_pass, A_WIFI_PASS_MAX - 1);
    return ESP_OK;
}

esp_err_t a_wifi_get_active_ssid(char *out, size_t len) {
    if (!out || len == 0) return ESP_ERR_INVALID_ARG;
    out[0] = '\0';
    if (s_mode == A_WIFI_MODE_AP) {
#if CONFIG_A_WIFI_ENABLE_AP
        wifi_config_t cur = {0};
        if (esp_wifi_get_config(WIFI_IF_AP, &cur) == ESP_OK) {
            strncpy(out, (const char *)cur.ap.ssid, len - 1);
            out[len - 1] = '\0';
            return ESP_OK;
        }
#endif
        return ESP_ERR_INVALID_STATE;
    }
#if CONFIG_A_WIFI_ENABLE_STA
    wifi_ap_record_t ap = {0};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        strncpy(out, (const char *)ap.ssid, len - 1);
        out[len - 1] = '\0';
        return ESP_OK;
    }
#endif
    return ESP_ERR_INVALID_STATE;
}

esp_err_t a_wifi_get_url(char *out, size_t len) {
    if (!out || len == 0) return ESP_ERR_INVALID_ARG;
    out[0] = '\0';
    esp_netif_t *nif = (s_mode == A_WIFI_MODE_AP) ? s_ap_netif : s_sta_netif;
    if (!nif) return ESP_ERR_INVALID_STATE;
    esp_netif_ip_info_t ip = {0};
    if (esp_netif_get_ip_info(nif, &ip) != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (ip.ip.addr == 0) return ESP_ERR_INVALID_STATE;
    snprintf(out, len, "http://" IPSTR "/", IP2STR(&ip.ip));
    return ESP_OK;
}

esp_err_t a_wifi_get_rssi(int8_t *rssi) {
#if !CONFIG_A_WIFI_ENABLE_STA
    (void)rssi;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!rssi) return ESP_ERR_INVALID_ARG;
    if (s_mode != A_WIFI_MODE_STA) return ESP_ERR_INVALID_STATE;
    wifi_ap_record_t ap = {0};
    esp_err_t r = esp_wifi_sta_get_ap_info(&ap);
    if (r == ESP_OK) *rssi = ap.rssi;
    return r;
#endif
}

esp_err_t a_wifi_scan(a_wifi_ap_t *aps, uint16_t max, uint16_t *out_n) {
#if !CONFIG_A_WIFI_ENABLE_STA
    (void)aps;
    (void)max;
    if (out_n) *out_n = 0;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!aps || max == 0 || !out_n) return ESP_ERR_INVALID_ARG;
    *out_n = 0;
    if (s_mode != A_WIFI_MODE_STA) return ESP_ERR_INVALID_STATE;

    wifi_scan_config_t sc = {0}; // active scan, all channels
    esp_err_t r = esp_wifi_scan_start(&sc, true);
    if (r != ESP_OK) return r;
    uint16_t found = 0;
    esp_wifi_scan_get_ap_num(&found);
    if (found == 0) return ESP_OK;
    if (found > max) found = max;
    wifi_ap_record_t *records = calloc(found, sizeof *records);
    if (!records) {
        esp_wifi_clear_ap_list();
        return ESP_ERR_NO_MEM;
    }
    r = esp_wifi_scan_get_ap_records(&found, records);
    if (r == ESP_OK) {
        for (uint16_t i = 0; i < found; i++) {
            strncpy(aps[i].ssid, (const char *)records[i].ssid, A_WIFI_SSID_MAX - 1);
            aps[i].ssid[A_WIFI_SSID_MAX - 1] = '\0';
            aps[i].rssi = records[i].rssi;
            aps[i].authmode = records[i].authmode;
        }
        *out_n = found;
    }
    free(records);
    return r;
#endif
}

// ---------------------------------------------------------------------------
//  init
// ---------------------------------------------------------------------------

static void seed_defaults_into_nvs(const a_wifi_cfg_t *cfg) {
    // STA seed — only if NVS empty.
    nvs_type_t t;
    if (a_nvs_find_key(NVS_KEY_STA_SSID, &t) != ESP_OK) {
        const char *s = cfg->sta_default_ssid;
        if (s && s[0]) {
            a_nvs_set_str(NVS_KEY_STA_SSID, s);
            a_nvs_set_str(NVS_KEY_STA_PASS, cfg->sta_default_pass ? cfg->sta_default_pass : "");
            info(TAG, "first-boot STA seed: ssid=\"%s\"", s);
        }
    }
    // Mode default — only if NVS empty.
    if (a_nvs_find_key(NVS_KEY_MODE, &t) != ESP_OK) {
        a_nvs_set_u8(NVS_KEY_MODE, (uint8_t)cfg->default_mode);
        info(TAG, "first-boot default mode: %s",
             cfg->default_mode == A_WIFI_MODE_AP ? "AP" : "STA");
    }
}

static void capture_ap_defaults(const a_wifi_cfg_t *cfg) {
    const char *ssid = cfg->ap_ssid;
    if (!ssid || !ssid[0]) {
        // Fall back to NVS "unit_name" (set by init_nvs_defaults from DEV_UNIT).
        size_t n = sizeof s_def_ap_ssid;
        if (a_nvs_get_str("unit_name", s_def_ap_ssid, &n) != ESP_OK || !s_def_ap_ssid[0]) {
            strncpy(s_def_ap_ssid, "atomic", sizeof s_def_ap_ssid - 1);
        }
    } else {
        strncpy(s_def_ap_ssid, ssid, sizeof s_def_ap_ssid - 1);
    }
    const char *pass = cfg->ap_pass;
    if (pass) {
        strncpy(s_def_ap_pass, pass, sizeof s_def_ap_pass - 1);
    }
}

esp_err_t a_wifi_init(const a_wifi_cfg_t *cfg) {
    if (s_inited) {
        warn(TAG, "a_wifi_init already called");
        return ESP_OK;
    }
    if (!cfg) return ESP_ERR_INVALID_ARG;

    s_mu = xSemaphoreCreateMutex();
    if (!s_mu) return ESP_ERR_NO_MEM;

    capture_ap_defaults(cfg);
    seed_defaults_into_nvs(cfg);

    // Create both netifs up-front. esp_wifi_set_mode binds the matching one
    // to its interface; the unused one just idles.
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t wic = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t r = esp_wifi_init(&wic);
    if (r != ESP_OK) {
        err(TAG, "esp_wifi_init failed: %s", esp_err_to_name(r));
        return r;
    }
    a_bits_set(BIT_WIFI_INIT);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL,
                                        NULL);
    esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, ip_event_handler, NULL, NULL);

    // Honour saved last TX-power (STA back-off persistence).
#if CONFIG_A_WIFI_ENABLE_STA
    uint8_t pwr;
    if (a_nvs_get_u8("wifi.pwr.last", &pwr) == ESP_OK && pwr <= 10) {
        s_power_level = (int8_t)pwr;
    }
#endif

    s_inited = true;

    a_wifi_mode_t mode = a_wifi_get_mode_pref();
    xSemaphoreTake(s_mu, portMAX_DELAY);
    r = apply_mode_internal(mode);
    xSemaphoreGive(s_mu);
    return r;
}
