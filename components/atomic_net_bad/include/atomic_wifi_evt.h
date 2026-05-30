#pragma once

#include "esp_err.h"
#include "esp_wifi_types.h"
#include "esp_netif_types.h"

const char * a_wifi_evt_name(wifi_event_t evt_id);
const char * a_wifi_err_reason(wifi_err_reason_t reason);
const char * a_ip_evt_name(ip_event_t evt_id);