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

static const char *TAG = "ㄱ";
static esp_netif_t *s_sta_netif = NULL;

static const int8_t WIFI_POWER_LEVEL[11] = {8, 20, 28, 34, 44, 52, 56, 60, 66, 72, 80};

static int8_t s_power_level = 10;

static esp_err_t a_wifi_set_power(void) {
    esp_err_t ok = esp_wifi_set_max_tx_power(WIFI_POWER_LEVEL[s_power_level]);
    if (ok != ESP_OK) {
        err(TAG, "esp_wifi_set_max_tx_power() err %d: %s", ok, esp_err_to_name(ok));
        return ok;
    }
    warn(TAG, "WiFi power level set to: %d(%d)", s_power_level, WIFI_POWER_LEVEL[s_power_level]);
    return ESP_OK;
}

static void a_wifi_event(void *handler_arg, esp_event_base_t evt_base, int32_t evt_id, void *evt_data) {
    notice(TAG, "wifi event %ld : %s", (long)evt_id, a_wifi_evt_name((wifi_event_t)evt_id));
    switch (evt_id) {
    case WIFI_EVENT_STA_START:
        a_bits_set(BIT_WIFI_START);
        break;
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
        wifi_event_sta_disconnected_t *evt = (wifi_event_sta_disconnected_t *)evt_data;
        warn(TAG, "WiFi Disconnect %d : %s", evt->reason, a_wifi_err_reason(evt->reason));
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

static void a_wifi_ip_event(void *handler_arg, esp_event_base_t evt_base, int32_t evt_id, void *evt_data) {
    notice(TAG, "a_wifi_ip_event %d : %s", evt_id, a_ip_evt_name(evt_id));
    switch (evt_id) {
    case IP_EVENT_STA_GOT_IP:
        notice(TAG, "got ip:" IPSTR, IP2STR(&((ip_event_got_ip_t *)evt_data)->ip_info.ip));
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
    try(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, a_wifi_event, NULL, NULL));
    try(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, a_wifi_ip_event, NULL, NULL));
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
    if (a_bits(BIT_WIFI_READY)) {
        warn(TAG, "a_wifi_connect(): already connected.");
        return ESP_OK;
    }
    if (a_bits(BIT_WIFI_WAIT)) {
        warn(TAG, "a_wifi_connect(): not called, already waiting for connection or timeout.");
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
    try(esp_wifi_set_mode(WIFI_MODE_STA));
    try(esp_wifi_set_config(WIFI_IF_STA, wifi_config));
    try(a_wifi_configure_power());
    a_bits_set(BIT_WIFI_INIT);
    try(esp_wifi_start());

    return ESP_OK;
}
