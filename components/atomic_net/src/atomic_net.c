// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// atomic_net — bring up the netif stack and hand off to atomic_wifi.
// MQTT and HA have been removed from this copy of atomic_net. NTP lives in
// atomic_ntp; a small task here waits on BIT_WIFI_READY then kicks it off
// (only when BIT_NTP_ENABLE is set by the caller).

#include "atomic_net.h"

#include "atomic_bits.h"
#include "atomic_err.h"
#include "atomic_log.h"
#include "atomic_ntp.h"
#include "atomic_wifi.h"
#include "esp_err.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ㄵ";

// Wait for an STA association, then run NTP. AP-only mode means STA never
// associates — task just sits until reboot. Cheap; not worth its own gate.
static void ntp_after_wifi_task(void *arg) {
    (void)arg;
    a_bits_wait(BIT_WIFI_READY);
    if (!a_bits(BIT_NTP_ENABLE)) {
        vTaskDelete(NULL);
        return;
    }
    while (!a_bits(BIT_NTP_READY)) {
        a_ntp_init();
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    vTaskDelete(NULL);
}

esp_err_t a_net_init(const a_wifi_cfg_t *cfg) {
    if (a_bits(BIT_NETIF_INIT)) {
        warn(TAG, "a_net_init already called");
        return ESP_OK;
    }
    info(TAG, "a_net_init()");
    try(esp_netif_init());
    a_bits_set(BIT_NETIF_INIT);
    try(a_wifi_init(cfg));
    xTaskCreate(ntp_after_wifi_task, "a_net_ntp", 3072, NULL, 4, NULL);
    return ESP_OK;
}
