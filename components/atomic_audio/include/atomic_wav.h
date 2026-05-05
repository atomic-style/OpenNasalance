#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "esp_err.h"
#include "atomic_audio.h"

typedef struct {
    char id[4];
    uint32_t size;
    uint16_t compression_code;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint32_t data_location;
    uint32_t data_size;
} wav_info_t;

esp_err_t atomic_wav_play(const char *filepath);

#ifdef __cplusplus
}
#endif
