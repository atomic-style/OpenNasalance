#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    int pin_clk;
    int pin_ws;
    int pin_data_1;
    int pin_data_2;
    int sample_rate;
} a_mic_441_config_t;

typedef struct {
    int pin_clk;
    int pin_data;
    int sample_rate;
    int buf_samples;
} a_mic_t5837_config_t;

esp_err_t a_mic_441_init(a_mic_441_config_t *cfg);
esp_err_t a_mic_t5837_init(a_mic_t5837_config_t *cfg);

// Blocking read of `frames` 32-bit samples from each of the two INMP441
// channels. `buf1` receives mic 1 (I2S master/data_1), `buf2` receives mic 2
// (I2S slave/data_2). Either buffer may be NULL to skip that channel.
esp_err_t a_mic_441_read_dual(int32_t *buf1, int32_t *buf2, size_t frames, int timeout_ms);
