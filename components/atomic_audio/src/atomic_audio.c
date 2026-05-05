#include "atomic_audio.h"
#include "atomic_err.h"
#include "atomic_i2s.h"
#include "atomic_log.h"
#include "atomic_wav.h"
#include "audio_player.h"
#include "esp_timer.h"
#include <string.h>
#include <strings.h>

static const char *TAG = "atomic_audio";

static audio_player_callback_event_t audio_event;
static QueueHandle_t event_queue = NULL;
static bool s_mp3_initialized = false;
static bool s_audio_initialized = false;

static esp_err_t atomic_audio_mute(AUDIO_PLAYER_MUTE_SETTING setting) {
    debug(TAG, "mute setting %d", setting);
    return ESP_OK;
}

static void atomic_audio_player_callback(audio_player_cb_ctx_t *ctx) {
    if (!ctx) {
        return;
    }
    audio_event = ctx->audio_event;
    if (event_queue) {
        xQueueSend(event_queue, &audio_event, 0);
    }
    audio_player_state_t state = audio_player_get_state();
    debug(TAG, "callback: event=%d, state=%d", audio_event, state);
}

static esp_err_t atomic_audio_player_i2s_configure(uint32_t sample_rate_hz, uint32_t bits_per_sample,
                                                   i2s_slot_mode_t slot_mode) {
    atomic_audio_config_t audio_config = {
        .bits_per_sample = bits_per_sample, .slot_mode = slot_mode, .sample_rate_hz = sample_rate_hz};
    esp_err_t ret = atomic_i2s_configure(&audio_config);
    if (ret != ESP_OK) {
        err(TAG, "I2S configure failed: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

static esp_err_t atomic_audio_player_init(void) {
    if (s_mp3_initialized) {
        warn(TAG, "MP3 player already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    audio_player_config_t player_config = {.mute_fn = atomic_audio_mute,
                                           .write_fn = atomic_i2s_write,
                                           .clk_set_fn = atomic_audio_player_i2s_configure,
                                           .priority = 5, // Higher priority for audio decode task
                                           .coreID = 0};
    esp_err_t ret = audio_player_new(player_config);
    if (ret != ESP_OK) {
        err(TAG, "audio_player_new failed: %s", esp_err_to_name(ret));
        return ret;
    }

    event_queue = xQueueCreate(1, sizeof(audio_player_callback_event_t));
    if (!event_queue) {
        err(TAG, "Failed to create event queue");
        audio_player_delete();
        return ESP_ERR_NO_MEM;
    }

    ret = audio_player_callback_register(atomic_audio_player_callback, NULL);
    if (ret != ESP_OK) {
        err(TAG, "audio_player_callback_register failed: %s", esp_err_to_name(ret));
        vQueueDelete(event_queue);
        event_queue = NULL;
        audio_player_delete();
        return ret;
    }

    s_mp3_initialized = true;
    return ESP_OK;
}

static const char *get_file_extension(const char *filepath) {
    const char *ext = strrchr(filepath, '.');
    return ext ? ext + 1 : NULL;
}

esp_err_t atomic_audio_play(const char *filepath) {
    if (!s_audio_initialized) {
        err(TAG, "Audio not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!filepath || filepath[0] == '\0') {
        err(TAG, "Invalid filepath");
        return ESP_ERR_INVALID_ARG;
    }

    int64_t start_time = esp_timer_get_time();
    const char *ext = get_file_extension(filepath);

    if (!ext) {
        err(TAG, "File has no extension: %s", filepath);
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Route to appropriate player based on file extension
    if (strcasecmp(ext, "wav") == 0) {
        esp_err_t ret = atomic_wav_play(filepath);
        if (ret == ESP_OK) {
            int64_t load_time = esp_timer_get_time() - start_time;
            info(TAG, "WAV playback started in %lld ms: %s", load_time / 1000, filepath);
        }
        return ret;
    } else if (strcasecmp(ext, "mp3") == 0) {
        if (!s_mp3_initialized) {
            err(TAG, "MP3 support not initialized");
            return ESP_ERR_INVALID_STATE;
        }

        // Stop any current playback before starting new one
        audio_player_state_t state = audio_player_get_state();
        if (state == AUDIO_PLAYER_STATE_PLAYING || state == AUDIO_PLAYER_STATE_PAUSE) {
            debug(TAG, "Stopping current playback");
            esp_err_t stop_ret = audio_player_stop();
            if (stop_ret != ESP_OK) {
                warn(TAG, "Failed to stop current playback: %s", esp_err_to_name(stop_ret));
            }
        }

        FILE *fp = fopen(filepath, "rb");
        if (!fp) {
            err(TAG, "Failed to open file: %s", filepath);
            return ESP_ERR_NOT_FOUND;
        }

        esp_err_t ret = audio_player_play(fp);
        if (ret != ESP_OK) {
            // If audio_player_play fails, we must close the file ourselves
            fclose(fp);
            err(TAG, "audio_player_play failed: %s", esp_err_to_name(ret));
            return ret;
        }

        int64_t load_time = esp_timer_get_time() - start_time;
        info(TAG, "MP3 playback started in %lld ms: %s", load_time / 1000, filepath);

        // File will be closed by audio_player when playback completes or on error
        return ESP_OK;
    } else {
        err(TAG, "Unsupported file type: .%s", ext);
        return ESP_ERR_NOT_SUPPORTED;
    }
}

esp_err_t atomic_audio_stop(void) {
    if (!s_audio_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_mp3_initialized) {
        return audio_player_stop();
    }
    // WAV playback is synchronous, nothing to stop
    return ESP_OK;
}

esp_err_t atomic_audio_pause(void) {
    if (!s_audio_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_mp3_initialized) {
        return audio_player_pause();
    }
    // WAV playback doesn't support pause
    return ESP_ERR_NOT_SUPPORTED;
}

audio_player_state_t atomic_audio_get_state(void) {
    if (!s_audio_initialized) {
        return AUDIO_PLAYER_STATE_SHUTDOWN;
    }
    if (s_mp3_initialized) {
        return audio_player_get_state();
    }
    return AUDIO_PLAYER_STATE_IDLE;
}

void atomic_audio_cleanup(void) {
    if (!s_audio_initialized) {
        return;
    }

    if (s_mp3_initialized) {
        audio_player_state_t state = audio_player_get_state();
        if (state == AUDIO_PLAYER_STATE_PLAYING || state == AUDIO_PLAYER_STATE_PAUSE) {
            audio_player_stop();
        }

        audio_player_delete();
        s_mp3_initialized = false;

        if (event_queue) {
            vQueueDelete(event_queue);
            event_queue = NULL;
        }
    }

    s_audio_initialized = false;
    info(TAG, "Audio cleanup complete");
}

esp_err_t atomic_audio_init(atomic_i2s_pin_config_t *pin_config) {
    if (!pin_config) {
        err(TAG, "Invalid pin_config");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_audio_initialized) {
        warn(TAG, "Audio already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = atomic_i2s_init(pin_config);
    if (ret != ESP_OK) {
        err(TAG, "I2S init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = atomic_audio_player_init();
    if (ret != ESP_OK) {
        err(TAG, "MP3 player init failed: %s", esp_err_to_name(ret));
        atomic_i2s_deinit();
        return ret;
    }

    s_audio_initialized = true;
    info(TAG, "Audio initialized (WAV + MP3 support)");
    return ESP_OK;
}