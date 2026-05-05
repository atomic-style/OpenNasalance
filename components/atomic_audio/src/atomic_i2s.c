#include "atomic_i2s.h"
#include "atomic_audio.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/i2s_types.h"

static const char *TAG = "atomic_i2s";

#define I2S_PORT 1
#define NUM_FRAMES 240
#define NUM_CHANNELS 2
#define DMA_DESC_NUM 32

DMA_ATTR __attribute__((aligned(16))) static int16_t atomic_i2s_buffer[NUM_FRAMES * NUM_CHANNELS];

static i2s_chan_handle_t s_atomic_i2s_channel;
static i2s_std_gpio_config_t s_atomic_i2s_gpio_config;
static atomic_audio_config_t s_atomic_audio_config;

static bool s_atomic_i2s_configured = false;
static bool s_atomic_i2s_enabled = false;

static esp_err_t atomic_i2s_enable(void) {
    if (s_atomic_i2s_enabled)
        return ESP_OK;
    ESP_ERROR_CHECK(i2s_channel_enable(s_atomic_i2s_channel));
    s_atomic_i2s_enabled = true;
    return ESP_OK;
}

static esp_err_t atomic_i2s_disable(void) {
    if (!s_atomic_i2s_enabled)
        return ESP_OK;
    ESP_ERROR_CHECK(i2s_channel_disable(s_atomic_i2s_channel));
    s_atomic_i2s_enabled = false;
    return ESP_OK;
}

static i2s_std_slot_config_t atomic_i2s_slot_config(atomic_audio_config_t *audio_config) {
    i2s_std_slot_config_t slot_config =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(audio_config->bits_per_sample, audio_config->slot_mode);
    return slot_config;
}

static i2s_std_clk_config_t atomic_i2s_clk_config(atomic_audio_config_t *audio_config) {
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(audio_config->sample_rate_hz);
    return clk_cfg;
}

static i2s_std_gpio_config_t atomic_i2s_gpio_config(atomic_i2s_pin_config_t *pin_config) {
    i2s_std_gpio_config_t gpio_config = {
        .bclk = pin_config->pin_bclk,
        .ws = pin_config->pin_ws,
        .dout = pin_config->pin_dout,
        .din = I2S_GPIO_UNUSED,
        .invert_flags =
            {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
    };
    return gpio_config;
}

static i2s_std_config_t atomic_i2s_std_config(atomic_audio_config_t *i2s_audio_config) {
    i2s_std_clk_config_t clk_cfg = atomic_i2s_clk_config(i2s_audio_config);
    i2s_std_slot_config_t slot_cfg = atomic_i2s_slot_config(i2s_audio_config);
    i2s_std_config_t i2s_std_config = {.clk_cfg = clk_cfg, .slot_cfg = slot_cfg, .gpio_cfg = s_atomic_i2s_gpio_config};
    return i2s_std_config;
}

static esp_err_t atomic_i2s_channel_init(void) {
    i2s_chan_config_t i2s_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
    i2s_chan_cfg.dma_desc_num = DMA_DESC_NUM;
    i2s_chan_cfg.dma_frame_num = NUM_FRAMES;
    i2s_chan_cfg.auto_clear = true;

    size_t total_buffer_bytes = DMA_DESC_NUM * NUM_FRAMES * NUM_CHANNELS * sizeof(int16_t);
    ESP_LOGI(TAG, "I2S DMA buffer: %d descriptors, %d frames/desc, %zu total bytes", DMA_DESC_NUM, NUM_FRAMES,
             total_buffer_bytes);

    return i2s_new_channel(&i2s_chan_cfg, &s_atomic_i2s_channel, NULL);
}

static esp_err_t atomic_i2s_reconfigure(atomic_audio_config_t *audio_config) {
    // Only disable if currently enabled - avoid disrupting playback
    bool was_enabled = s_atomic_i2s_enabled;
    if (was_enabled) {
        ESP_ERROR_CHECK(atomic_i2s_disable());
    }
    s_atomic_audio_config = *audio_config;
    const i2s_std_config_t std_cfg = atomic_i2s_std_config(audio_config);
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(s_atomic_i2s_channel, &std_cfg.clk_cfg));
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_slot(s_atomic_i2s_channel, &std_cfg.slot_cfg));
    if (was_enabled) {
        ESP_ERROR_CHECK(atomic_i2s_enable());
    }
    return ESP_OK;
}

static esp_err_t atomic_i2s_check_reconfigure(atomic_audio_config_t *audio_config) {
    if ((audio_config->bits_per_sample != s_atomic_audio_config.bits_per_sample) ||
        (audio_config->sample_rate_hz != s_atomic_audio_config.sample_rate_hz) ||
        (audio_config->slot_mode != s_atomic_audio_config.slot_mode)) {
        ESP_ERROR_CHECK(atomic_i2s_reconfigure(audio_config));
    }
    return ESP_OK;
}

esp_err_t atomic_i2s_write_file(FILE *file, size_t data_size) {
    if (!file) {
        ESP_LOGE(TAG, "Invalid file handle");
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_atomic_i2s_channel) {
        ESP_LOGE(TAG, "I2S not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t bytes_played = 0;
    size_t bytes_read;
    size_t bytes_written = 0;

    esp_err_t ret = atomic_i2s_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S: %s", esp_err_to_name(ret));
        return ret;
    }

    while (bytes_played < data_size) {
        bytes_read = fread(atomic_i2s_buffer, 1, (NUM_FRAMES * NUM_CHANNELS * sizeof(int16_t)), file);
        if (bytes_read == 0) {
            if (ferror(file)) {
                ESP_LOGE(TAG, "File read error");
                return ESP_ERR_INVALID_RESPONSE;
            }
            break; // EOF
        }

        ret = i2s_channel_write(s_atomic_i2s_channel, atomic_i2s_buffer, bytes_read, &bytes_written, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(ret));
            return ret;
        }

        bytes_played += bytes_written;
    }

    return ESP_OK;
}

esp_err_t atomic_i2s_write(void *data_buffer, size_t data_length, size_t *bytes_written, uint32_t timeout_ms) {
    ESP_ERROR_CHECK(atomic_i2s_enable());
    esp_err_t ret = i2s_channel_write(s_atomic_i2s_channel, data_buffer, data_length, bytes_written, timeout_ms);
    return ret;
}

esp_err_t atomic_i2s_configure(atomic_audio_config_t *audio_config) {
    if (s_atomic_i2s_configured) {
        ESP_ERROR_CHECK(atomic_i2s_check_reconfigure(audio_config));
    } else {
        s_atomic_audio_config.bits_per_sample = audio_config->bits_per_sample;
        s_atomic_audio_config.slot_mode = audio_config->slot_mode;
        s_atomic_audio_config.sample_rate_hz = audio_config->sample_rate_hz;
        const i2s_std_config_t std_cfg = atomic_i2s_std_config(&s_atomic_audio_config);
        ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_atomic_i2s_channel, &std_cfg));
    }
    s_atomic_i2s_configured = true;
    return ESP_OK;
}

esp_err_t atomic_i2s_init(atomic_i2s_pin_config_t *i2s_pin_config) {
    if (!i2s_pin_config) {
        ESP_LOGE(TAG, "Invalid pin_config");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_atomic_i2s_channel) {
        ESP_LOGW(TAG, "I2S already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "atomic_i2s_init()");
    s_atomic_audio_config.bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT;
    s_atomic_audio_config.slot_mode = I2S_SLOT_MODE_STEREO;
    s_atomic_audio_config.sample_rate_hz = 44100;
    s_atomic_i2s_gpio_config = atomic_i2s_gpio_config(i2s_pin_config);

    esp_err_t ret = atomic_i2s_channel_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S channel init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t atomic_i2s_deinit(void) {
    if (!s_atomic_i2s_channel) {
        return ESP_ERR_INVALID_STATE;
    }

    atomic_i2s_disable();

    esp_err_t ret = i2s_del_channel(s_atomic_i2s_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }

    s_atomic_i2s_channel = NULL;
    s_atomic_i2s_configured = false;
    s_atomic_i2s_enabled = false;

    ESP_LOGI(TAG, "I2S deinitialized");
    return ESP_OK;
}
