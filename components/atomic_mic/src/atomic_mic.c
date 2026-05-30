#include "atomic_mic.h"

#include "atomic_err.h"
#include "atomic_log.h"
#include "hal/i2s_types.h"
#include "driver/i2s_pdm.h"

#define SAMPLE_RATE 24000
#define BUF_SAMPLES 256

#define PDM_PCM_RX_FREQ_HZ 24000   // I2S PDM RX frequency in PCM format
#define PDM_RAW_RX_FREQ_HZ 2048000 // I2S PDM RX over sample frequency in raw PDM format

static const char *TAG = "㇐";

static i2s_chan_handle_t rx_chan;

// I2S in PDM mode
// Check peripheral documentation for supported modes and channels per platform.
// The esp32s3 can handle PDM on I2S 0 and 1, but PDM-to-PCM only on I2S 0.
// The esp32p4 has hardware filters. On earlier platforms, use software decimation and filters.

// PDM to PCM steps:
// 1. Low Pass (FIR) filter to restore analog wave.
// 2. Downsample to PCM sample rate.
// 3. High-pass filter to remove DC offset.
// 4. Amplify converted format to adjust gain.

// Data width is always 16 bits, i.e.:
// CH0 0x1234, CH1 0x5678, CH0 0x9abc, CH1 0xdef0.

//
// PDM RX Mode notes:
//
// First, set i2s_pdm_rx_slot_config_t::data_fmt to i2s_pdm_data_fmt_t::I2S_PDM_DATA_FMT_RAW
// Then set sample rate, which defines t5837 mode:
// i2s_pdm_rx_clk_config_t::sample_rate_hz
//
// T5837 Mode clock values:
// 400 kHz to 800 kHz = Low-Power Mode
// 2.0 MHz to 3.7 MHz = High Quality Mode
// 4.2 MHz to 4.8 MHz = Ultrasonic Mode
//
// PDM slot configuration - use macro I2S_PDM_RX_SLOT_RAW_FMT_DEFAULT_CONFIG
//
//
//
// i2s configuratino steps (check state: registered, ready, running):
// 1. i2s_new_channel -> registered ->
// 2. i2s_channel_init_<mode> -> ready ->
// 3. i2s_channel_enable -> running ->
// 4. i2s_channel_read

esp_err_t atomic_mic_init(atomic_mic_config_t *cfg) {
    // chan_cfg
    ESP_LOGI(TAG, "atomic_mic_init - creating i2s channel.");
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t ok = i2s_new_channel(&chan_cfg, NULL, &rx_chan);
    if (ok != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel() error %s", esp_err_to_name(ok));
        return ok;
    }

    // slot_cfg
    i2s_pdm_rx_slot_config_t slot_cfg =
        (cfg->format == I2S_PDM_DATA_FMT_PCM)
            ? (i2s_pdm_rx_slot_config_t)I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(
                  I2S_DATA_BIT_WIDTH_16BIT, cfg->mode)
            : (i2s_pdm_rx_slot_config_t)I2S_PDM_RX_SLOT_RAW_FMT_DEFAULT_CONFIG(
                  I2S_DATA_BIT_WIDTH_16BIT, cfg->mode);

    // clk_cfg
    i2s_pdm_rx_clk_config_t clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(
        (cfg->format == I2S_PDM_DATA_FMT_PCM) ? PDM_PCM_RX_FREQ_HZ : PDM_RAW_RX_FREQ_HZ);
    clk_cfg.dn_sample_mode = I2S_PDM_DSR_16S;

    // gpio_cfg
    i2s_pdm_rx_gpio_config_t gpio_cfg = {.clk = cfg->pin_clk,
                                         .dins[0] = cfg->pin_data_1,
                                         .dins[1] = cfg->pin_data_2,
                                         .invert_flags = {0}};

    // pdm_cfg
    i2s_pdm_rx_config_t pdm_cfg = {
        clk_cfg,
        slot_cfg,
        gpio_cfg,
    };

    // uint32_t pdm_clk_hz = (uint32_t)SAMPLE_RATE * 128u;

    ESP_LOGI(TAG, "atomic_mic_init() - initializing pdm rx channel.");
    ok = i2s_channel_init_pdm_rx_mode(rx_chan, &pdm_cfg);
    if (ok != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_pdm_rx_mode() error %s", esp_err_to_name(ok));
        return ok;
    }

    ESP_LOGI(TAG, "atomic_mic_init - i2s_channel_enable.");
    ok = i2s_channel_enable(rx_chan);
    if (ok != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable() error %s", esp_err_to_name(ok));
        return ok;
    }

    ESP_LOGI(TAG, "atomic_mic_init() success");
    return ESP_OK;
}
/*
void i2s_example_pdm_rx_task(void *args) {
    int16_t *r_buf = (int16_t *)calloc(1, EXAMPLE_BUFF_SIZE);
    assert(r_buf);
    i2s_chan_handle_t rx_chan = i2s_example_init_pdm_rx();

    size_t r_bytes = 0;

while (1) {
    // Read i2s data
    if (i2s_channel_read(rx_chan, r_buf, EXAMPLE_BUFF_SIZE, &r_bytes, 1000) == ESP_OK) {
        printf("Read Task: i2s read %d bytes\n-----------------------------------\n", r_bytes);
        printf("[0] %d [1] %d [2] %d [3] %d\n[4] %d [5] %d [6] %d [7] %d\n\n", r_buf[0], r_buf[1],
               r_buf[2], r_buf[3], r_buf[4], r_buf[5], r_buf[6], r_buf[7]);
    } else {
        printf("Read Task: i2s read failed\n");
    }
    vTaskDelay(pdMS_TO_TICKS(200));
}
free(r_buf);
vTaskDelete(NULL);
}
*/