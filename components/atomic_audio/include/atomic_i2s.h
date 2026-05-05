#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "hal/i2s_types.h"
#include <stdio.h>

typedef struct {
    int pin_bclk;
    int pin_ws;
    int pin_dout;
} atomic_i2s_pin_config_t;

typedef struct {
    i2s_data_bit_width_t bits_per_sample; // 8, 16, 24, 32
    i2s_slot_mode_t slot_mode;            // I2S_SLOT_MODE_MONO or I2S_SLOT_MODE_STEREO
    uint32_t sample_rate_hz;              // 8000, 16000, 32000, 44100, 48000
} atomic_audio_config_t;

esp_err_t atomic_i2s_write_file(FILE *file, size_t data_size);
esp_err_t atomic_i2s_write(void *data_buffer, size_t data_length, size_t *bytes_written, uint32_t timeout_ms);
esp_err_t atomic_i2s_configure(atomic_audio_config_t *audio_config);
esp_err_t atomic_i2s_init(atomic_i2s_pin_config_t *i2s_pin_config);
esp_err_t atomic_i2s_deinit(void);

#ifdef __cplusplus
}
#endif
