// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include "driver/gpio.h"
#include "esp_err.h"
#include "soc/gpio_num.h"

typedef void (*atomic_gpio_callback_t)(uint32_t gpio_num, int val);

typedef struct {
    gpio_num_t gpio_num;
    int val;
    uint64_t last;
    atomic_gpio_callback_t callback;
} atomic_gpio_pin_config_t;

esp_err_t atomic_gpio_config_in(gpio_num_t gpio_num, bool pullup, bool pulldown, gpio_int_type_t intr_type, void (*cb)(uint32_t gpio_num, int val));
esp_err_t atomic_gpio_config_out(gpio_num_t gpio_num, bool pullup, bool pulldown);
esp_err_t atomic_gpio_set(gpio_num_t gpio_num, uint32_t level);
int atomic_gpio_get(gpio_num_t gpio_num);
