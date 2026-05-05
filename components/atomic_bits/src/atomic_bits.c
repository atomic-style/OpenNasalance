// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "esp_log.h"
#include "atomic_bits.h"
#include "atomic_log.h"
#include "atomic_err.h"

static const char *TAG = "】";

EventGroupHandle_t atomic_bits;

enum
{
    EVENT_BITS = 24,
    ALL_BITS = ((1 << 24) - 1)
};

static void a_bits_reset(void)
{
    xEventGroupClearBits(atomic_bits, ALL_BITS); 
}

bool a_bits(EventBits_t bit)
{
    return (bool) ((xEventGroupGetBits(atomic_bits) & bit) != 0);
}

void a_bits_set(EventBits_t bits)
{
    xEventGroupSetBits(atomic_bits, bits);
}

void a_bits_wait(EventBits_t bit)
{
    xEventGroupWaitBits(atomic_bits, bit, pdFALSE, pdTRUE, portMAX_DELAY);
}

void a_bits_clear(EventBits_t bits)
{
    xEventGroupClearBits(atomic_bits, bits); 
}

esp_err_t a_bits_init(void) {
    debug(TAG, "a_bits_init()");
    atomic_bits = xEventGroupCreate();
    a_bits_reset();
    a_bits_set(BIT_INIT);
    return ESP_OK;
}
/*
void atomic_bits_set(EventBits_t bits)
{
    xEventGroupSetBits(atomic_bits, bits);
}

void atomic_bits_clear(EventBits_t bits)
{
    xEventGroupClearBits(atomic_bits, bits); 
}

void atomic_bits_wait(EventBits_t bit)
{
    xEventGroupWaitBits(atomic_bits, bit, pdFALSE, pdTRUE, portMAX_DELAY);
}

EventBits_t atomic_bits_get_all(void)
{
    return xEventGroupGetBits(atomic_bits);
}

EventBits_t atomic_bits_get_bit(EventBits_t bit)
{
    return xEventGroupGetBits(atomic_bits) & bit;
}

int atomic_bits_get_as_int(EventBits_t bit) {
    return (int) ((xEventGroupGetBits(atomic_bits) & bit) != 0) ? 1 : 0;
}

bool atomic_bits_get_as_bool(EventBits_t bit) {
    return (bool) ((xEventGroupGetBits(atomic_bits) & bit) != 0);
}

void atomic_bits_print(EventBits_t bit)
{
    char bit_char[2] = {0};
    bit_char[0] = (xEventGroupGetBits(atomic_bits) & bit) ? '1' : '0';
    bit_char[1] = '\0';
    ESP_LOGI(TAG, "atomic_bits: bit %lu: %s", (unsigned long)bit, bit_char);
}

void atomic_bits_debug(void)
{
    EventBits_t bits = xEventGroupGetBits(atomic_bits);
    char bits_char[EVENT_BITS + 1] = {0};
    for (int i = 0; i < EVENT_BITS; i++) {
        bits_char[i] = (bits & (1 << i)) ? '1' : '0';
    }
    bits_char[EVENT_BITS] = '\0';
    ESP_LOGI(TAG, "atomic_bits: %s", bits_char);
}
*/

