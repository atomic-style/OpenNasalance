#include "atomic_wifi.h"
#include "atomic_bits.h"
#include "atomic_err.h"
#include "atomic_log.h"
#include "atomic_net.h"
#include "atomic_nvs.h"
#include "atomic_wifi_evt.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "lwip/ip4_addr.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "ㄱ";
static esp_netif_t *s_sta_netif = NULL;
static a_wifi_mode_t s_active_mode = A_WIFI_MODE_STA;

#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
static esp_netif_t *s_ap_netif = NULL;

#define A_WIFI_AP_DEFAULT_PASS "12345678"
#define A_WIFI_AP_DEFAULT_SSID "atomic-ap"
// WPA2 requires 8+ chars; if the configured AP password is shorter we
// silently fall back to an open network rather than refusing to start.
#define A_WIFI_AP_MIN_PASS 8

// AP-side network. We override the ESP default (192.168.4.1) so devices
// joining it land on a stable, well-known address.
#define A_WIFI_AP_IP4_A 192
#define A_WIFI_AP_IP4_B 168
#define A_WIFI_AP_IP4_C 1
#define A_WIFI_AP_IP4_D 1

static esp_err_t a_wifi_start_ap(void);
#endif // CONFIG_ESP_WIFI_SOFTAP_SUPPORT

static esp_err_t a_wifi_start_sta(void);

static const int8_t WIFI_POWER_LEVEL[11] = {8,  20, 28, 34, 44, 52,
                                            56, 60, 66, 72, 80};

static int8_t s_power_level = 10;

static esp_err_t a_wifi_set_power(void) {
  esp_err_t ok = esp_wifi_set_max_tx_power(WIFI_POWER_LEVEL[s_power_level]);
  if (ok != ESP_OK) {
    err(TAG, "esp_wifi_set_max_tx_power() err %d: %s", ok, esp_err_to_name(ok));
    return ok;
  }
  warn(TAG, "WiFi power level set to: %d(%d)", s_power_level,
       WIFI_POWER_LEVEL[s_power_level]);
  return ESP_OK;
}

static void a_wifi_event(void *handler_arg, esp_event_base_t evt_base,
                         int32_t evt_id, void *evt_data) {
  notice(TAG, "wifi event %ld : %s", (long)evt_id,
         a_wifi_evt_name((wifi_event_t)evt_id));
  switch (evt_id) {
  case WIFI_EVENT_STA_START:
    a_bits_set(BIT_WIFI_START);
    break;
  case WIFI_EVENT_STA_STOP:
    a_bits_clear(BIT_WIFI_START);
    a_bits_clear(BIT_WIFI_READY);
    a_bits_clear(BIT_WIFI_WAIT);
    break;
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
  case WIFI_EVENT_AP_START:
    info(TAG, "AP started");
    a_bits_set(BIT_WIFI_AP_READY);
    break;
  case WIFI_EVENT_AP_STOP:
    info(TAG, "AP stopped");
    a_bits_clear(BIT_WIFI_AP_READY);
    break;
  case WIFI_EVENT_AP_STACONNECTED: {
    wifi_event_ap_staconnected_t *c = (wifi_event_ap_staconnected_t *)evt_data;
    info(TAG, "AP client connected: aid=%u " MACSTR, c->aid, MAC2STR(c->mac));
    break;
  }
  case WIFI_EVENT_AP_STADISCONNECTED: {
    wifi_event_ap_stadisconnected_t *d =
        (wifi_event_ap_stadisconnected_t *)evt_data;
    info(TAG, "AP client disconnected: aid=%u " MACSTR, d->aid,
         MAC2STR(d->mac));
    break;
  }
#endif // CONFIG_ESP_WIFI_SOFTAP_SUPPORT
  case WIFI_EVENT_STA_CONNECTED: {
    wifi_event_sta_connected_t *cevt = (wifi_event_sta_connected_t *)evt_data;
    info(TAG, "STA connected: SSID=\"%.*s\" channel=%u bssid=" MACSTR,
         cevt->ssid_len, cevt->ssid, cevt->channel, MAC2STR(cevt->bssid));
    break;
  }
  case WIFI_EVENT_STA_DISCONNECTED:
    a_bits_clear(BIT_WIFI_READY);
    a_bits_clear(BIT_WIFI_WAIT);
    a_bits_clear(BIT_MQTT_READY);
    a_bits_clear(BIT_HA_READY);
    wifi_event_sta_disconnected_t *evt =
        (wifi_event_sta_disconnected_t *)evt_data;
    warn(TAG, "WiFi Disconnect %d : %s", evt->reason,
         a_wifi_err_reason(evt->reason));
    // Dynamic TX-power scaling disabled. Re-enable for low-power
    // boards (e.g. Xiao Seeed Studio S3) where back-off is useful.
    // if (s_power_level > 0) {
    //     s_power_level--;
    //     try(a_wifi_set_power());
    // } else {
    //     s_power_level = 10;
    //     try(a_wifi_set_power());
    // }
    break;
  default:
    break;
  }
}

static void a_wifi_ip_event(void *handler_arg, esp_event_base_t evt_base,
                            int32_t evt_id, void *evt_data) {
  notice(TAG, "a_wifi_ip_event %d : %s", evt_id, a_ip_evt_name(evt_id));
  switch (evt_id) {
  case IP_EVENT_STA_GOT_IP:
    notice(TAG, "got ip:" IPSTR,
           IP2STR(&((ip_event_got_ip_t *)evt_data)->ip_info.ip));
    a_bits_set(BIT_WIFI_READY);
    a_nvs_set_u8("wifi.pwr.last", s_power_level);
    break;
  default:
    break;
  }
}

static esp_err_t a_wifi_configure(void) {
  s_sta_netif = esp_netif_create_default_wifi_sta();
  wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t ret = esp_wifi_init(&wifi_init_config);
  if (ret != ESP_OK) {
    err(TAG, "esp_wifi_init() failed: %s", esp_err_to_name(ret));
  }
  return ret;
}

static esp_err_t a_wifi_register_handlers(void) {
  try(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                          a_wifi_event, NULL, NULL));
  try(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                          a_wifi_ip_event, NULL, NULL));
  return ESP_OK;
}

static esp_err_t a_wifi_configure_power(void) {
  uint8_t wifi_power_default = 0;
  uint8_t wifi_power_last = 0;

  // Get default power level
  esp_err_t ok = a_nvs_get_u8("wifi.pwr.dflt", &wifi_power_default);
  if (ok != ESP_OK) {
    uint8_t device_id;
    ok = a_nvs_get_u8("device_id", &device_id);
    if (ok != ESP_OK) {
      warn(TAG, "device_id not found. Using default: 0");
      device_id = 0;
    }
    if (device_id == 4) {
      wifi_power_default = 6;
    } else {
      wifi_power_default = 10;
    }
    a_nvs_set_u8("wifi.pwr.dflt", wifi_power_default);
    warn(TAG, "wifi.pwr.dflt not found. Using default: %d", wifi_power_default);
  }
  info(TAG, "wifi.pwr.dflt: %d", wifi_power_default);

  ok = a_nvs_get_u8("wifi.pwr.last", &wifi_power_last);
  if (ok != ESP_OK) {
    warn(TAG, "wifi.pwr.last not found. Using default: %d", wifi_power_default);
    wifi_power_last = wifi_power_default;
  } else {
    s_power_level = wifi_power_last;
    if (wifi_power_last < wifi_power_default) {
      s_power_level++;
      info(TAG, "Increasing WiFi power level to: %d", s_power_level);
    }
  }
  info(TAG, "Setting WiFi power level to: %d", s_power_level);
  return ESP_OK;
}

esp_err_t a_wifi_connect(void) {
  if (s_active_mode != A_WIFI_MODE_STA) {
    return ESP_ERR_INVALID_STATE;
  }
  if (a_bits(BIT_WIFI_READY)) {
    warn(TAG, "a_wifi_connect(): already connected.");
    return ESP_OK;
  }
  if (a_bits(BIT_WIFI_WAIT)) {
    warn(TAG, "a_wifi_connect(): not called, already waiting for connection or "
              "timeout.");
    return ESP_OK;
  }
  if (!a_bits(BIT_WIFI_START)) {
    warn(TAG, "a_wifi_connect(): Station not started. Waiting...");
  }
  a_bits_wait(BIT_WIFI_START);
  a_bits_set(BIT_WIFI_WAIT);

  try(a_wifi_set_power());

  info(TAG, "a_wifi_connect() -> esp_wifi_connect()");
  esp_err_t ret = esp_wifi_connect();
  if (ret != ESP_OK) {
    err(TAG, "esp_wifi_connect() err %d: %s", ret, esp_err_to_name(ret));
    a_bits_clear(BIT_WIFI_WAIT);
  }
  return ret;
}

esp_err_t a_wifi_init(wifi_config_t *wifi_config) {
  if (a_bits(BIT_WIFI_INIT)) {
    warn(TAG, "a_wifi_init() already called.");
    return ESP_OK;
  }
  debug(TAG, "a_wifi_init()");
  try(a_wifi_configure());
  try(a_wifi_register_handlers());

  a_wifi_mode_t pref = a_wifi_get_mode_pref();
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
  if (pref == A_WIFI_MODE_AP) {
    // Persisted preference is AP — start there. STA netif still gets
    // created (a_wifi_configure handles that) so it's ready if the user
    // toggles back later.
    a_bits_set(BIT_WIFI_INIT);
    s_active_mode = A_WIFI_MODE_AP;
    try(a_wifi_configure_power());
    return a_wifi_start_ap();
  }
#else
  (void)pref;
#endif // CONFIG_ESP_WIFI_SOFTAP_SUPPORT

  try(esp_wifi_set_mode(WIFI_MODE_STA));

  // Stored credentials override the compile-time config when present.
  char nvs_ssid[A_WIFI_SSID_MAX] = {0};
  char nvs_pass[A_WIFI_PASS_MAX] = {0};
  if (a_wifi_load_credentials(nvs_ssid, nvs_pass) == ESP_OK && nvs_ssid[0]) {
    notice(TAG, "Using stored credentials for SSID '%s'", nvs_ssid);
    memset(wifi_config->sta.ssid, 0, sizeof wifi_config->sta.ssid);
    memset(wifi_config->sta.password, 0, sizeof wifi_config->sta.password);
    strncpy((char *)wifi_config->sta.ssid, nvs_ssid,
            sizeof wifi_config->sta.ssid - 1);
    strncpy((char *)wifi_config->sta.password, nvs_pass,
            sizeof wifi_config->sta.password - 1);
  }

  try(esp_wifi_set_config(WIFI_IF_STA, wifi_config));
  try(a_wifi_configure_power());
  a_bits_set(BIT_WIFI_INIT);
  s_active_mode = A_WIFI_MODE_STA;
  try(esp_wifi_start());

  return ESP_OK;
}

esp_err_t a_wifi_disconnect(void) {
  info(TAG, "a_wifi_disconnect()");
  a_bits_clear(BIT_WIFI_WAIT);
  return esp_wifi_disconnect();
}

esp_err_t a_wifi_scan(a_wifi_ap_t *out, uint16_t max, uint16_t *count) {
  if (!out || !count || max == 0)
    return ESP_ERR_INVALID_ARG;
  *count = 0;

  if (!a_bits(BIT_WIFI_INIT)) {
    warn(TAG, "scan: wifi not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  wifi_scan_config_t cfg = {0};
  try(esp_wifi_scan_start(&cfg, /*block=*/true));

  uint16_t found = 0;
  try(esp_wifi_scan_get_ap_num(&found));
  if (found == 0)
    return ESP_OK;

  wifi_ap_record_t *recs = calloc(found, sizeof *recs);
  if (!recs)
    return ESP_ERR_NO_MEM;
  esp_err_t r = esp_wifi_scan_get_ap_records(&found, recs);
  if (r != ESP_OK) {
    free(recs);
    return r;
  }

  uint16_t kept = 0;
  for (uint16_t i = 0; i < found && kept < max; i++) {
    const char *ssid = (const char *)recs[i].ssid;
    if (!ssid[0])
      continue; // hidden / blank

    bool dup = false;
    for (uint16_t j = 0; j < kept; j++) {
      if (strncmp(out[j].ssid, ssid, A_WIFI_SSID_MAX) == 0) {
        dup = true;
        break;
      }
    }
    if (dup)
      continue;

    strncpy(out[kept].ssid, ssid, A_WIFI_SSID_MAX - 1);
    out[kept].ssid[A_WIFI_SSID_MAX - 1] = '\0';
    out[kept].rssi = recs[i].rssi;
    out[kept].authmode = (uint8_t)recs[i].authmode;
    kept++;
  }
  free(recs);
  *count = kept;
  return ESP_OK;
}

esp_err_t a_wifi_save_credentials(const char *ssid, const char *password) {
  if (!ssid || !password)
    return ESP_ERR_INVALID_ARG;
  try(a_nvs_set_str("wifi.ssid", ssid));
  try(a_nvs_set_str("wifi.pass", password));
  notice(TAG, "saved credentials for '%s'", ssid);
  return ESP_OK;
}

esp_err_t a_wifi_load_credentials(char *ssid_out, char *pass_out) {
  if (!ssid_out || !pass_out)
    return ESP_ERR_INVALID_ARG;
  nvs_type_t t;
  if (a_nvs_find_key("wifi.ssid", &t) != ESP_OK)
    return ESP_ERR_NOT_FOUND;
  size_t s_len = A_WIFI_SSID_MAX;
  size_t p_len = A_WIFI_PASS_MAX;
  try(a_nvs_get_str("wifi.ssid", ssid_out, &s_len));
  if (a_nvs_get_str("wifi.pass", pass_out, &p_len) != ESP_OK) {
    pass_out[0] = '\0';
  }
  return ESP_OK;
}

esp_err_t a_wifi_forget(void) {
  info(TAG, "a_wifi_forget()");
  a_nvs_erase_key("wifi.ssid");
  a_nvs_erase_key("wifi.pass");
  if (a_bits(BIT_WIFI_READY) || a_bits(BIT_WIFI_WAIT)) {
    a_wifi_disconnect();
  }
  return ESP_OK;
}

esp_err_t a_wifi_apply_credentials(const char *ssid, const char *password) {
  if (!ssid)
    return ESP_ERR_INVALID_ARG;
  try(a_wifi_save_credentials(ssid, password ? password : ""));

  wifi_config_t cfg = {.sta = {.threshold.authmode = WIFI_AUTH_OPEN}};
  strncpy((char *)cfg.sta.ssid, ssid, sizeof cfg.sta.ssid - 1);
  if (password && password[0]) {
    strncpy((char *)cfg.sta.password, password, sizeof cfg.sta.password - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  }

  if (a_bits(BIT_WIFI_READY) || a_bits(BIT_WIFI_WAIT)) {
    a_wifi_disconnect();
  }
  try(esp_wifi_set_config(WIFI_IF_STA, &cfg));
  return a_wifi_connect();
}

// ---------------------------------------------------------------------------
//  STA / AP mode
// ---------------------------------------------------------------------------

a_wifi_mode_t a_wifi_get_mode_pref(void) {
  nvs_type_t t;
  if (a_nvs_find_key("wifi.mode", &t) != ESP_OK)
    return A_WIFI_MODE_STA;
  uint8_t v = 0;
  if (a_nvs_get_u8("wifi.mode", &v) != ESP_OK)
    return A_WIFI_MODE_STA;
  return (v == A_WIFI_MODE_AP) ? A_WIFI_MODE_AP : A_WIFI_MODE_STA;
}

esp_err_t a_wifi_set_mode_pref(a_wifi_mode_t mode) {
  return a_nvs_set_u8("wifi.mode", (uint8_t)mode);
}

a_wifi_mode_t a_wifi_get_mode(void) { return s_active_mode; }

#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
esp_err_t a_wifi_load_ap_credentials(char *ssid_out, char *pass_out) {
  if (!ssid_out || !pass_out)
    return ESP_ERR_INVALID_ARG;
  size_t s_len = A_WIFI_SSID_MAX;
  size_t p_len = A_WIFI_PASS_MAX;
  nvs_type_t t;

  // SSID: explicit AP SSID > project name (set by atomic_ui at boot)
  // > built-in fallback.
  if (a_nvs_find_key("wifi.ap.ssid", &t) == ESP_OK) {
    a_nvs_get_str("wifi.ap.ssid", ssid_out, &s_len);
  } else if (a_nvs_find_key("dev.project", &t) == ESP_OK) {
    a_nvs_get_str("dev.project", ssid_out, &s_len);
  } else {
    strncpy(ssid_out, A_WIFI_AP_DEFAULT_SSID, A_WIFI_SSID_MAX - 1);
    ssid_out[A_WIFI_SSID_MAX - 1] = '\0';
  }

  if (a_nvs_find_key("wifi.ap.pass", &t) == ESP_OK) {
    a_nvs_get_str("wifi.ap.pass", pass_out, &p_len);
  } else {
    strncpy(pass_out, A_WIFI_AP_DEFAULT_PASS, A_WIFI_PASS_MAX - 1);
    pass_out[A_WIFI_PASS_MAX - 1] = '\0';
  }
  return ESP_OK;
}

esp_err_t a_wifi_save_ap_credentials(const char *ssid, const char *password) {
  if (!ssid)
    return ESP_ERR_INVALID_ARG;
  try(a_nvs_set_str("wifi.ap.ssid", ssid));
  try(a_nvs_set_str("wifi.ap.pass", password ? password : ""));
  notice(TAG, "saved AP credentials for '%s'", ssid);
  return ESP_OK;
}

static esp_err_t a_wifi_start_ap(void) {
  if (!s_ap_netif)
    s_ap_netif = esp_netif_create_default_wifi_ap();

  // Move the AP onto our chosen subnet (default 192.168.1.1/24). DHCPS
  // must be stopped before set_ip_info or the call returns ESP_ERR_*.
  esp_netif_dhcps_stop(s_ap_netif);
  esp_netif_ip_info_t ip_info;
  IP4_ADDR(&ip_info.ip, A_WIFI_AP_IP4_A, A_WIFI_AP_IP4_B, A_WIFI_AP_IP4_C,
           A_WIFI_AP_IP4_D);
  IP4_ADDR(&ip_info.gw, A_WIFI_AP_IP4_A, A_WIFI_AP_IP4_B, A_WIFI_AP_IP4_C,
           A_WIFI_AP_IP4_D);
  IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
  esp_netif_set_ip_info(s_ap_netif, &ip_info);
  esp_netif_dhcps_start(s_ap_netif);

  char ssid[A_WIFI_SSID_MAX] = {0};
  char pass[A_WIFI_PASS_MAX] = {0};
  a_wifi_load_ap_credentials(ssid, pass);

  bool wpa = pass[0] && strnlen(pass, A_WIFI_PASS_MAX) >= A_WIFI_AP_MIN_PASS;
  if (pass[0] && !wpa) {
    warn(TAG, "AP password too short for WPA2 (<%d chars) — using OPEN",
         A_WIFI_AP_MIN_PASS);
  }
  wifi_config_t ap_cfg = {
      .ap = {
          .channel = 1,
          .max_connection = 4,
          .authmode = wpa ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN,
      }};
  strncpy((char *)ap_cfg.ap.ssid, ssid, sizeof ap_cfg.ap.ssid - 1);
  ap_cfg.ap.ssid_len = (uint8_t)strnlen(ssid, sizeof ap_cfg.ap.ssid);
  if (wpa) {
    strncpy((char *)ap_cfg.ap.password, pass, sizeof ap_cfg.ap.password - 1);
  }

  try(esp_wifi_set_mode(WIFI_MODE_AP));
  try(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
  try(esp_wifi_start());
  notice(TAG, "AP up: ssid='%s' auth=%s", ssid, pass[0] ? "WPA2" : "OPEN");
  return ESP_OK;
}
#endif // CONFIG_ESP_WIFI_SOFTAP_SUPPORT

static esp_err_t a_wifi_start_sta(void) {
  if (!s_sta_netif)
    s_sta_netif = esp_netif_create_default_wifi_sta();

  char nvs_ssid[A_WIFI_SSID_MAX] = {0};
  char nvs_pass[A_WIFI_PASS_MAX] = {0};
  wifi_config_t cfg = {.sta = {.threshold.authmode = WIFI_AUTH_OPEN}};
  if (a_wifi_load_credentials(nvs_ssid, nvs_pass) == ESP_OK && nvs_ssid[0]) {
    strncpy((char *)cfg.sta.ssid, nvs_ssid, sizeof cfg.sta.ssid - 1);
    if (nvs_pass[0]) {
      strncpy((char *)cfg.sta.password, nvs_pass, sizeof cfg.sta.password - 1);
      cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }
  }
  try(esp_wifi_set_mode(WIFI_MODE_STA));
  try(esp_wifi_set_config(WIFI_IF_STA, &cfg));
  try(esp_wifi_start());
  return cfg.sta.ssid[0] ? a_wifi_connect() : ESP_OK;
}

esp_err_t a_wifi_apply_mode(a_wifi_mode_t mode) {
  notice(TAG, "a_wifi_apply_mode(%s)", mode == A_WIFI_MODE_AP ? "AP" : "STA");
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
  if (mode == A_WIFI_MODE_AP) {
    warn(TAG, "AP mode requested but SoftAP support is disabled "
              "(CONFIG_ESP_WIFI_SOFTAP_SUPPORT=n)");
    return ESP_ERR_NOT_SUPPORTED;
  }
#endif
  a_wifi_set_mode_pref(mode);

  // Flip s_active_mode *first* so the reconnect loop and a_wifi_connect
  // stop trying to drive the radio while we tear it down.
  s_active_mode = mode;

  // Tear STA-side state cleanly. The WIFI_EVENT_STA_STOP handler also
  // clears these on its own, but doing it eagerly avoids a race where
  // a_net_task could see stale flags between disconnect and stop.
  a_bits_clear(BIT_WIFI_READY);
  a_bits_clear(BIT_WIFI_WAIT);
  a_bits_clear(BIT_WIFI_START);
  a_bits_clear(BIT_NTP_READY);
  a_bits_clear(BIT_MQTT_READY);
  a_bits_clear(BIT_HA_READY);

  esp_wifi_disconnect();
  esp_wifi_stop();

#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
  if (mode == A_WIFI_MODE_AP)
    return a_wifi_start_ap();
#endif
  return a_wifi_start_sta();
}

esp_err_t a_wifi_get_url(char *out, size_t len) {
  if (!out || len < 8)
    return ESP_ERR_INVALID_ARG;
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
  esp_netif_t *nif =
      (s_active_mode == A_WIFI_MODE_AP) ? s_ap_netif : s_sta_netif;
#else
  esp_netif_t *nif = s_sta_netif;
#endif
  if (!nif) {
    snprintf(out, len, "http://0.0.0.0");
    return ESP_ERR_INVALID_STATE;
  }
  esp_netif_ip_info_t ip = {0};
  esp_err_t r = esp_netif_get_ip_info(nif, &ip);
  if (r != ESP_OK) {
    snprintf(out, len, "http://0.0.0.0");
    return r;
  }
  // For STA, esp_netif_get_ip_info returns the assigned IP after DHCP. For
  // AP it returns the gateway (default 192.168.4.1).
  snprintf(out, len, "http://" IPSTR, IP2STR(&ip.ip));
  return ESP_OK;
}

esp_err_t a_wifi_get_rssi(int8_t *out) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  *out = 0;
  if (s_active_mode != A_WIFI_MODE_STA || !a_bits(BIT_WIFI_READY)) {
    return ESP_ERR_INVALID_STATE;
  }
  wifi_ap_record_t rec;
  esp_err_t r = esp_wifi_sta_get_ap_info(&rec);
  if (r != ESP_OK)
    return r;
  *out = rec.rssi;
  return ESP_OK;
}

esp_err_t a_wifi_get_active_ssid(char *out, size_t len) {
  if (!out || len == 0)
    return ESP_ERR_INVALID_ARG;
  out[0] = '\0';
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
  if (s_active_mode == A_WIFI_MODE_AP) {
    wifi_config_t cfg = {0};
    try(esp_wifi_get_config(WIFI_IF_AP, &cfg));
    strncpy(out, (const char *)cfg.ap.ssid, len - 1);
    out[len - 1] = '\0';
    return ESP_OK;
  }
#endif
  if (a_bits(BIT_WIFI_READY)) {
    wifi_ap_record_t rec;
    if (esp_wifi_sta_get_ap_info(&rec) == ESP_OK) {
      strncpy(out, (const char *)rec.ssid, len - 1);
      out[len - 1] = '\0';
      return ESP_OK;
    }
  }
  wifi_config_t cfg = {0};
  if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK) {
    strncpy(out, (const char *)cfg.sta.ssid, len - 1);
    out[len - 1] = '\0';
  }
  return ESP_OK;
}
