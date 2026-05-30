#include "atomic_net.h"
#include "atomic_bits.h"
#include "atomic_err.h"
#include "atomic_ha.h"
#include "atomic_log.h"
#include "atomic_mqtt.h"
#include "atomic_ntp.h"
#include "atomic_wifi.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ㄵ";

static esp_err_t a_net_init_netif(void) {
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK) {
        err(TAG, "esp_netif_init() FAIL: %s", esp_err_to_name(ret));
        return ret;
    }
    a_bits_set(BIT_NETIF_INIT);
    return ret;
}

static void a_net_task(void *arg) {
    notice(TAG, "a_net_task()");
    uint32_t num_errors = 0;
    uint32_t loop_delay = 2000;
    while (1) {
        // STA reconnect / NTP / MQTT / HA all assume we're a station with
        // outbound connectivity. In AP mode none of that applies — sleep
        // quietly so we don't churn the radio (esp_wifi_connect would
        // return ESP_ERR_WIFI_MODE every iteration).
        if (a_wifi_get_mode() != A_WIFI_MODE_STA) {
            num_errors = 0;
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        if (!a_bits(BIT_WIFI_READY)) {
            debug(TAG, "a_net_task: calling a_wifi_connect(). Errors: %d", num_errors);
            a_wifi_connect();
            num_errors++;
        } else if (a_bits(BIT_NTP_ENABLE) && (!a_bits(BIT_NTP_READY))) {
            debug(TAG, "a_net_task: calling a_ntp_init(). Errors: %d", num_errors);
            a_ntp_init();
            num_errors++;
        } else if (a_bits(BIT_MQTT_ENABLE) && (!a_bits(BIT_MQTT_READY))) {
            debug(TAG, "a_net_task: calling a_mqtt_connect(). Errors: %d", num_errors);
            atomic_mqtt_init();
            num_errors++;
        } else if (a_bits(BIT_HA_ENABLE) && (!a_bits(BIT_HA_READY))) {
            debug(TAG, "a_net_task: calling a_ha_init(). Errors: %d", num_errors);
            a_ha_init();
            num_errors++;
        } else {
            num_errors = 0;
            // debug(TAG, "a_net_task: ready. Errors: %d", num_errors);
            loop_delay = 10000;
        }
        if (num_errors > 0) {
            loop_delay = 3000;
        }
        vTaskDelay(loop_delay / portTICK_PERIOD_MS);
    }
}

static esp_err_t a_net_task_init(void) {
    if (a_bits(BIT_NET_TASK)) {
        warn(TAG, "a_net_task already running.");
        return ESP_ERR_INVALID_STATE;
    }
    info(TAG, "a_net_task_init()");
    xTaskCreate(a_net_task, "a_net_task", 4096, NULL, 5, NULL);
    return ESP_OK;
}

esp_err_t a_net_init(wifi_config_t *wifi_config) {
    if (a_bits(BIT_NETIF_INIT)) {
        warn(TAG, "atomic_net_init() already called.");
        return ESP_OK;
    }
    debug(TAG, "a_net_init()");
    try(a_net_init_netif());
    try(a_wifi_init(wifi_config));
    a_bits_wait(BIT_WIFI_START);
    try(a_net_task_init());
    return ESP_OK;
}