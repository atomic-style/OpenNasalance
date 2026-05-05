#include "audio_player.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "atomic_mp3.h"

/***
https://github.com/chmorgan/esp-audio-player
https://github.com/chmorgan/esp-libhelix-mp3
https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/peripherals/i2s.html
***/
/*

static const char *TAG = "atomic_mp3";

#define CONFIG_BSP_I2S_NUM 1

static i2s_std_gpio_config_t s_i2s_gpio_config;

#define BSP_I2S_DUPLEX_MONO_CFG(_sample_rate)                                                         \
    {                                                                                                 \
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(_sample_rate),                                          \
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO), \
        .gpio_cfg = s_i2s_gpio_config,                                                                 \
    }

static i2s_chan_handle_t i2s_tx_chan;
static i2s_chan_handle_t i2s_rx_chan;

static audio_player_callback_event_t audio_event;
static QueueHandle_t event_queue;

static esp_err_t bsp_i2s_write(void * audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    esp_err_t ret = ESP_OK;
    ret = i2s_channel_write(i2s_tx_chan, (char *)audio_buffer, len, bytes_written, timeout_ms);
    return ret;
}

static esp_err_t bsp_i2s_reconfig_clk(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    esp_err_t ret = ESP_OK;
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG((i2s_data_bit_width_t)bits_cfg, (i2s_slot_mode_t)ch),
        .gpio_cfg = s_i2s_gpio_config,
    };

    ret |= i2s_channel_disable(i2s_tx_chan);
    ret |= i2s_channel_reconfig_std_clock(i2s_tx_chan, &std_cfg.clk_cfg);
    ret |= i2s_channel_reconfig_std_slot(i2s_tx_chan, &std_cfg.slot_cfg);
    ret |= i2s_channel_enable(i2s_tx_chan);
    return ret;
}

static esp_err_t audio_mute_function(AUDIO_PLAYER_MUTE_SETTING setting) {
    ESP_LOGI(TAG, "mute setting %d", setting);
    return ESP_OK;
}


static esp_err_t atomic_mp3_init_i2s(const i2s_std_config_t *i2s_config, i2s_chan_handle_t *tx_channel, i2s_chan_handle_t *rx_channel)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; // Auto clear the legacy data in the DMA buffer
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, tx_channel, rx_channel));

    const i2s_std_config_t std_cfg_default = BSP_I2S_DUPLEX_MONO_CFG(22050);
    const i2s_std_config_t *p_i2s_cfg = &std_cfg_default;
    if (i2s_config != NULL) {
        p_i2s_cfg = i2s_config;
    }

    if (tx_channel != NULL) {
        ESP_ERROR_CHECK(i2s_channel_init_std_mode(*tx_channel, p_i2s_cfg));
        ESP_ERROR_CHECK(i2s_channel_enable(*tx_channel));
        ESP_LOGI(TAG, "tx channel enabled");
    }
    if (rx_channel != NULL) {
        ESP_ERROR_CHECK(i2s_channel_init_std_mode(*rx_channel, p_i2s_cfg));
        ESP_ERROR_CHECK(i2s_channel_enable(*rx_channel));
        ESP_LOGI(TAG, "rx channel enabled");
    }


    return ESP_OK;
}

static void audio_player_callback(audio_player_cb_ctx_t *ctx)
{
    ESP_LOGW(TAG, "audio_player_callback %d", ctx->audio_event);
    audio_event = ctx->audio_event;
    xQueueSend(event_queue, &audio_event, 0);

    audio_player_state_t state = audio_player_get_state();
    ESP_LOGW(TAG, "audio_player_state: %d", state);
}

esp_err_t atomic_mp3_test(void)
{
    audio_player_config_t config = { .mute_fn = audio_mute_function,
                                     .write_fn = bsp_i2s_write,
                                     .clk_set_fn = bsp_i2s_reconfig_clk,
                                     .priority = 0,
                                     .coreID = 0 };
    esp_err_t ret = audio_player_new(config);
    ESP_LOGI(TAG, "audio_player_new ret %d", ret);

    ret = audio_player_delete();
    ESP_LOGI(TAG, "audio_player_delete ret %d", ret);

    audio_player_state_t state = audio_player_get_state();
    ESP_LOGI(TAG, "audio_player_state %d", state);

    return ret;
}

esp_err_t atomic_mp3_play(const char *filepath)
{
    ESP_LOGI(TAG, "atomic_mp3_play(%s)", filepath);
    return ESP_OK;
}



esp_err_t atomic_mp3_init(void)
{
    ESP_LOGI(TAG, "atomic_mp3_init()");

    s_i2s_gpio_config.mclk = -1;
    s_i2s_gpio_config.bclk = 43;
    s_i2s_gpio_config.ws = 44;
    s_i2s_gpio_config.dout = 16;
    s_i2s_gpio_config.din = -1;
    s_i2s_gpio_config.invert_flags.mclk_inv = false;
    s_i2s_gpio_config.invert_flags.bclk_inv = false;
    s_i2s_gpio_config.invert_flags.ws_inv = false;

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = s_i2s_gpio_config,
    };
    esp_err_t ret = atomic_mp3_init_i2s(&std_cfg, &i2s_tx_chan, &i2s_rx_chan);
    ESP_LOGW(TAG, "atomic_mp3_init_i2s: %s", (ret == ESP_OK) ? "OK" : "ERROR");

    audio_player_config_t config = { .mute_fn = audio_mute_function,
                                     .write_fn = bsp_i2s_write,
                                     .clk_set_fn = bsp_i2s_reconfig_clk,
                                     .priority = 0,
                                     .coreID = 0 };
    ret = audio_player_new(config);
    ESP_LOGW(TAG, "audio_player_new: %s", (ret == ESP_OK) ? "OK" : "ERROR");

    event_queue = xQueueCreate(1, sizeof(audio_player_callback_event_t));
    ESP_LOGW(TAG, "xQueueCreate: %s", (ret == ESP_OK) ? "OK" : "ERROR");

    ret = audio_player_callback_register(audio_player_callback, NULL);
    ESP_LOGW(TAG, "audio_player_callback_register: %s", (ret == ESP_OK) ? "OK" : "ERROR");

    audio_player_state_t state = audio_player_get_state();
    ESP_LOGW(TAG, "state: %d", state); // 0=IDLE

    char filename[] = "/sd/sfx/misc/spongebob.mp3";
    //char filename[] = "/sd/sfx/trek/access.wav";

    // size_t mp3_size = (mp3_end - mp3_start) - 1;
    // ESP_LOGI(TAG, "mp3_size %zu bytes", mp3_size);

    // FILE *fp = fmemopen((void*)mp3_start, mp3_size, "rb");
    FILE *fp = fopen(filename, "rb");
    ESP_LOGW(TAG, "fopen: %s", (fp != NULL) ? "OK" : "ERROR");

    ret = audio_player_play(fp);
    ESP_LOGW(TAG, "audio_player_play: %s", (ret == ESP_OK) ? "OK" : "ERROR");

    vTaskDelay(pdMS_TO_TICKS(1000));

    return ret;
}
*/