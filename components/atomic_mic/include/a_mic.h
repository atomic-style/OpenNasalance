#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    int pin_clk;
    int pin_data;
} a_mic_config_t;

typedef struct {
    int pin_clk;
    int pin_ws;
    int pin_data_1;
    int pin_data_2;
    int sample_rate;
} a_mic_441_config_t;

typedef struct {
    int pin_clk;
    int pin_data_1;  // mic1 DATA pin (PDM RX LINE0). Always used.
    int pin_data_2;  // mic2 DATA pin (PDM RX LINE1). Used only in stereo mode;
                     // ignored otherwise.
    int sample_rate;
    int buf_samples;
    int gain;     // software gain multiplier applied on read (1 = unity). The
                  // S3 PDM->PCM path has no hardware amplifier, so quiet MEMS
                  // capture is boosted here (with clipping) before the DSP/WAV
                  // stages.
    bool stereo;  // false: one mic on pin_data_1, mono fan-out (both buf1 and
                  // buf2 get identical data). true: two T5837s, each on its
                  // own DATA pin (LINE0 + LINE1 on the S3 PDM RX), both with
                  // SELECT=GND. De-interleaved so buf1 = mic1, buf2 = mic2.
} a_mic_t5837_config_t;

esp_err_t a_mic_init(a_mic_config_t *a_mic_config);
esp_err_t a_mic_441_init(a_mic_441_config_t *cfg);
esp_err_t a_mic_t5837_init(a_mic_t5837_config_t *cfg);

// Blocking read of `frames` 32-bit samples from each of the two INMP441
// channels. `buf1` receives mic 1 (I2S master/data_1), `buf2` receives mic 2
// (I2S slave/data_2). Either buffer may be NULL to skip that channel.
esp_err_t a_mic_441_read_dual(int32_t *buf1, int32_t *buf2, size_t frames, int timeout_ms);

// Blocking read of `frames` PCM samples from the single PDM T5837 mic. Each
// 16-bit sample is promoted into the high half of an int32 word (sample << 16)
// to match the 441 sample layout the nasometer DSP/WAV paths expect. While only
// one mic is fitted, `buf1` and `buf2` receive identical data (mono fan-out);
// either buffer may be NULL to skip it.
esp_err_t a_mic_t5837_read_dual(int32_t *buf1, int32_t *buf2, size_t frames, int timeout_ms);
