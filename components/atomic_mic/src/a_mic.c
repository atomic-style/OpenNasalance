#include "a_mic.h"
#include "hal/i2s_types.h"
#include "driver/i2s_common.h"
#include "driver/i2s_pdm.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/task.h"

// #define SAMPLE_RATE 24000
#define BUF_SAMPLES 240

#define PDM_PCM_RX_FREQ_HZ 24000   // I2S PDM RX frequency in PCM format
#define PDM_RAW_RX_FREQ_HZ 3072000 // I2S PDM RX over sample frequency in raw PDM format
// #define PROCESS_BUFFER_SIZE 240 * 6

static const char *TAG = "㇐";

static i2s_chan_handle_t rx_chan;
static uint32_t s_buffer_size;
static int16_t *s_buf;
static i2s_pdm_data_fmt_t s_format;
static TaskHandle_t xHandle;

// PDM to PCM steps:
// 1. Low Pass (FIR) filter to restore analog wave.
// 2. Downsample to PCM sample rate.
// 3. High-pass filter to remove DC offset.
// 4. Amplify converted format to adjust gain.

// Data width is always 16 bits, i.e.:
// CH0 0x1234, CH1 0x5678, CH0 0x9abc, CH1 0xdef0.
//
// T5837 Mode clock values:
// 400 kHz to 800 kHz = Low-Power Mode
// 2.0 MHz to 3.7 MHz = High Quality Mode
// 4.2 MHz to 4.8 MHz = Ultrasonic Mode

static void print_bits16(const char *label, int16_t v) {
    uint16_t u = (uint16_t)v;
    char bits[20]; // 16 digits + 3 spaces + NUL
    int p = 0;
    for (int i = 15; i >= 0; i--) {
        bits[p++] = (u & (1u << i)) ? '1' : '0';
        if (i % 4 == 0 && i != 0)
            bits[p++] = ' '; // space between nibbles
    }
    bits[p] = '\0';
    printf("%s %s  (%6d  0x%04X)\n", label, bits, v, u);
}

static void print_pdm16(const char *label, uint16_t u) {
    char bits[20];
    int p = 0;
    for (int i = 15; i >= 0; i--) {
        bits[p++] = (u & (1u << i)) ? '1' : '0';
        if (i % 4 == 0 && i != 0)
            bits[p++] = ' ';
    }
    bits[p] = '\0';
    printf("%s %s  0x%04X\n", label, bits, u);
}

static void a_mic_task(void *arg) {
    ESP_LOGI(TAG, "starting task");
    uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    s_buf = (int16_t *)heap_caps_malloc(s_buffer_size, caps);
    size_t bytes_read = 0;
    while (1) {
        ESP_LOGI(TAG, "ping");
        esp_err_t ok = i2s_channel_read(rx_chan, s_buf, s_buffer_size, &bytes_read, 1000);
        if (ok != ESP_OK) {
            ESP_LOGE(TAG, "i2s_channel_read() error %s", esp_err_to_name(ok));
        } else {
            if (s_format == I2S_PDM_DATA_FMT_PCM) {
                print_bits16("L[0]", s_buf[0]);
                print_bits16("R[1]", s_buf[1]);
                print_bits16("L[2]", s_buf[2]);
                print_bits16("R[3]", s_buf[3]);
            } else {
                print_pdm16("[0]", (uint16_t)s_buf[0]);
                print_pdm16("[1]", (uint16_t)s_buf[1]);
                print_pdm16("[2]", (uint16_t)s_buf[2]);
                print_pdm16("[3]", (uint16_t)s_buf[3]);
            }
            printf("\n");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t a_mic_init(a_mic_config_t *cfg) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_chan));
    /*
        i2s_pdm_rx_slot_config_t slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,
            .slot_mode = (cfg->slot == I2S_PDM_SLOT_BOTH) ? I2S_SLOT_MODE_STEREO :
       I2S_SLOT_MODE_MONO, .slot_mask = cfg->slot, .data_fmt = cfg->format};
    */
    i2s_pdm_rx_slot_config_t slot_cfg = (i2s_pdm_rx_slot_config_t)I2S_PDM_RX_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    slot_cfg.slot_mask = cfg->slot;

    i2s_pdm_rx_clk_config_t clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(
        (cfg->format == I2S_PDM_DATA_FMT_PCM) ? PDM_PCM_RX_FREQ_HZ : PDM_RAW_RX_FREQ_HZ);
    clk_cfg.dn_sample_mode = I2S_PDM_DSR_16S;

    i2s_pdm_rx_gpio_config_t gpio_cfg = {
        .clk = cfg->pin_clk, .dins[0] = cfg->pin_data, .invert_flags = {0}};

    i2s_pdm_rx_config_t pdm_cfg = {
        clk_cfg,
        slot_cfg,
        gpio_cfg,
    };

    ESP_ERROR_CHECK(i2s_channel_init_pdm_rx_mode(rx_chan, &pdm_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));

    s_buffer_size = BUF_SAMPLES * sizeof(int16_t);
    s_format = cfg->format;

    BaseType_t xRet = xTaskCreate(a_mic_task, "a_mic_task", 3072, NULL, 5, NULL);
    if (xRet != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate() err: %d", xRet);
        return ESP_FAIL;
    }
    return ESP_OK;
}
