#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *device_name;
    const char *device_unit;
    const char *device_model;
    const char *device_chip_id;
    const char *mqtt_topic;
} a_ha_config_t;

esp_err_t a_ha_publish_state(void);
esp_err_t a_ha_config(a_ha_config_t *config);
esp_err_t a_ha_init(void);
