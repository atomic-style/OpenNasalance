#pragma once

#include "esp_err.h"
#include "esp_wifi_types.h"
#include "atomic_ntp.h"
// #include "atomic_mqtt.h"   // disabled — espressif/mqtt not present in this build

esp_err_t a_net_init(wifi_config_t *wifi_config);

