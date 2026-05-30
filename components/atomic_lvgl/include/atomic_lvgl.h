#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef struct a_lvgl_cfg_s {
    int h_res;
    int v_res;
    bool sw_rotation;
} a_lvgl_cfg_t;

esp_err_t atomic_lvgl_init(a_lvgl_cfg_t *a_lvgl_cfg);