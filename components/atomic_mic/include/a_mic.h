#pragma once

#include "esp_err.h"
#include "driver/gpio.h" // IWYU pragma: keep
#include "hal/i2s_types.h"

typedef enum {
    MIC_FMT_PCM = 0, // hardware PDM->PCM (I2S0 only); read 16-bit PCM
    MIC_FMT_PDM = 1, // raw PDM bitstream; decode to PCM in software
} a_mic_fmt_t;

typedef struct {
    gpio_num_t pin_clk;
    gpio_num_t pin_data;
    i2s_pdm_data_fmt_t format; // I2S_PDM_DATA_FMT_PCM, I2S_PDM_DATA_FMT_RAW
    i2s_pdm_slot_mask_t slot;  // I2S_PDM_SLOT_LEFT, I2S_PDM_SLOT_RIGHT, or I2S_PDM_SLOT_BOTH
} a_mic_config_t;

esp_err_t a_mic_init(a_mic_config_t *cfg);
