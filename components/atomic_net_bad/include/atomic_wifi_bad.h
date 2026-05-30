#pragma once

#include <stdint.h>

#include "atomic_net.h"
#include "esp_err.h"

#define A_WIFI_SSID_MAX  33
#define A_WIFI_PASS_MAX  65

typedef struct {
    char     ssid[A_WIFI_SSID_MAX];
    int8_t   rssi;
    uint8_t  authmode; // wifi_auth_mode_t
} a_wifi_ap_t;

esp_err_t a_wifi_connect(void);
esp_err_t a_wifi_init(wifi_config_t *wifi_config);

// Disconnect from the current AP without tearing down the netif.
esp_err_t a_wifi_disconnect(void);

// Synchronous active scan. Caller-supplied buffer; `*count` returns the
// number of unique APs written (capped at `max`).
esp_err_t a_wifi_scan(a_wifi_ap_t *out, uint16_t max, uint16_t *count);

// Persist credentials to NVS under "wifi.ssid" / "wifi.pass".
esp_err_t a_wifi_save_credentials(const char *ssid, const char *password);

// Load credentials from NVS. Buffers must be at least A_WIFI_SSID_MAX /
// A_WIFI_PASS_MAX. Returns ESP_ERR_NOT_FOUND if no creds stored.
esp_err_t a_wifi_load_credentials(char *ssid_out, char *pass_out);

// Wipe stored credentials. Disconnects if currently connected.
esp_err_t a_wifi_forget(void);

// Apply credentials and (re)connect. Saves to NVS first.
esp_err_t a_wifi_apply_credentials(const char *ssid, const char *password);

// ---------------------------------------------------------------------------
//  STA / AP mode
// ---------------------------------------------------------------------------

typedef enum {
    A_WIFI_MODE_STA = 0,
    A_WIFI_MODE_AP  = 1,
} a_wifi_mode_t;

// Persisted mode preference (NVS key "wifi.mode"). Defaults to STA.
a_wifi_mode_t a_wifi_get_mode_pref(void);
esp_err_t     a_wifi_set_mode_pref(a_wifi_mode_t mode);

// Mode the radio is actually running in right now (which may differ from
// the persisted preference during a transition).
a_wifi_mode_t a_wifi_get_mode(void);

// Tear down the current radio state and bring it back up in the given mode.
// In STA: uses saved/compile-time creds and connects. In AP: starts SoftAP
// with credentials from a_wifi_load_ap_credentials(), or sane defaults.
esp_err_t a_wifi_apply_mode(a_wifi_mode_t mode);

// AP credentials persisted under "wifi.ap.ssid" / "wifi.ap.pass". An empty
// password means "open network".
esp_err_t a_wifi_load_ap_credentials(char *ssid_out, char *pass_out);
esp_err_t a_wifi_save_ap_credentials(const char *ssid, const char *password);

// Format the active URL ("http://<ip>") for the current mode into `out`.
// In STA returns "http://0.0.0.0" if not yet connected; in AP returns the
// gateway IP of the AP netif.
esp_err_t a_wifi_get_url(char *out, size_t len);

// Current STA RSSI in dBm. Returns ESP_ERR_INVALID_STATE if not connected
// or not in STA mode.
esp_err_t a_wifi_get_rssi(int8_t *out);

// Currently active SSID for the running mode (the AP we're connected to in
// STA, or the AP we're broadcasting in AP mode).
esp_err_t a_wifi_get_active_ssid(char *out, size_t len);
