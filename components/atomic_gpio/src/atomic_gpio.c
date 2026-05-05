// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "atomic_gpio.h"
#include "atomic_err.h"
#include "atomic_log.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "soc/gpio_num.h"

static const char *TAG = "㇢";

#define MAX_INPUT_PINS 10

static QueueHandle_t s_atomic_gpio_evt_queue;
static bool atomic_gpio_ready = false;
static atomic_gpio_pin_config_t atomic_gpio_cb[MAX_INPUT_PINS] = {0};

static void IRAM_ATTR atomic_gpio_isr_handler(void *arg) {
    gpio_num_t gpio_num = (gpio_num_t)(intptr_t)arg;
    BaseType_t task_woken = pdFALSE;
    xQueueSendFromISR(s_atomic_gpio_evt_queue, &gpio_num, &task_woken);
    if (task_woken == pdTRUE)
        portYIELD_FROM_ISR();
}

static void atomic_gpio_fire_callback(gpio_num_t gpio_num) {
    atomic_gpio_pin_config_t *entry = &atomic_gpio_cb[gpio_num];
    entry->callback(gpio_num, entry->val);
}

static void atomic_gpio_event_task(void *arg) {
    gpio_num_t gpio_num;
    for (;;) {
        if (xQueueReceive(s_atomic_gpio_evt_queue, &gpio_num, portMAX_DELAY)) {
            int level = gpio_get_level(gpio_num);
            notice(TAG, "gpio_num %d changed, level=%d", gpio_num, level);
            atomic_gpio_cb[gpio_num].val = level;
            atomic_gpio_fire_callback(gpio_num);
        }
    }
}

static esp_err_t atomic_gpio_init(void) {
    if (atomic_gpio_ready)
        return ESP_OK;
    s_atomic_gpio_evt_queue = xQueueCreate(10, sizeof(gpio_num_t));
    try(gpio_install_isr_service(0));
    xTaskCreate(atomic_gpio_event_task, "atomic_gpio_evt", 3072, NULL, 5, NULL);
    atomic_gpio_ready = true;
    return ESP_OK;
}

static esp_err_t atomic_gpio_register(gpio_num_t gpio_num, void (*cb)(uint32_t gpio_num, int val)) {
    try(atomic_gpio_init());
    atomic_gpio_cb[gpio_num] = (atomic_gpio_pin_config_t){.gpio_num = gpio_num, .val = 0, .last = 0, .callback = *cb};
    try(gpio_isr_handler_add(gpio_num, atomic_gpio_isr_handler, (void *)gpio_num));
    try(gpio_intr_enable(gpio_num));
    return ESP_OK;
}

esp_err_t atomic_gpio_config_in(gpio_num_t gpio_num, bool pullup, bool pulldown, gpio_int_type_t intr_type,
                                void (*cb)(uint32_t gpio_num, int val)) {
    if (cb) {
        atomic_gpio_register(gpio_num, cb);
    }
    gpio_config_t cfg = {.pin_bit_mask = 1ULL << gpio_num,
                         .mode = GPIO_MODE_INPUT,
                         .pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
                         .pull_down_en = pulldown ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
                         .intr_type = intr_type};
    return gpio_config(&cfg);
}

esp_err_t atomic_gpio_config_out(gpio_num_t gpio_num, bool pullup, bool pulldown) {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = pulldown ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

esp_err_t atomic_gpio_set(gpio_num_t gpio_num, uint32_t level) { return gpio_set_level(gpio_num, level); }

int atomic_gpio_get(gpio_num_t gpio_num) { return gpio_get_level(gpio_num); }