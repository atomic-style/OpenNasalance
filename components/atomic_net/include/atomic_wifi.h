#pragma once

#include "atomic_net.h"
#include "esp_err.h"


esp_err_t a_wifi_connect(void);
esp_err_t a_wifi_init(wifi_config_t *wifi_config);
