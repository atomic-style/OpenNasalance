#pragma once

#include "esp_err.h"
#include "hal/i2s_types.h"

typedef struct {
    i2s_pdm_data_fmt_t format;
    i2s_slot_mode_t mode;
    int pin_clk;
    int pin_data_1; // mic1 DATA pin (PDM RX LINE0). Always used.
    int pin_data_2; // mic2 DATA pin (PDM RX LINE1). Used only in stereo mode;
                    // ignored otherwise.
    // int sample_rate;
    int buf_samples;
    int gain; // software gain multiplier applied on read (1 = unity). The
              // S3 PDM->PCM path has no hardware amplifier, so quiet MEMS
              // capture is boosted here (with clipping) before the DSP/WAV
              // stages.

} atomic_mic_config_t;

esp_err_t atomic_mic_init(atomic_mic_config_t *cfg);
esp_err_t atomic_mic_read(int32_t *buf1, int32_t *buf2, size_t frames, int timeout_ms);
