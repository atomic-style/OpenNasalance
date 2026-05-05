#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "atomic_i2s.h"
#include "audio_player.h"
#include "esp_err.h"

esp_err_t atomic_audio_play(const char *filepath);
esp_err_t atomic_audio_stop(void);
esp_err_t atomic_audio_pause(void);
audio_player_state_t atomic_audio_get_state(void);
void atomic_audio_cleanup(void);
esp_err_t atomic_audio_init(atomic_i2s_pin_config_t *pin_config);

#ifdef __cplusplus
}
#endif
