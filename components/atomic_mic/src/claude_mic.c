#include "atomic_mic.h"

#include "driver/i2s_common.h"
#include "driver/i2s_pdm.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"

#include <string.h>

static const char *TAG = "㇐";

#define DEF_READ_FRAMES 256
#define DEF_DMA_DESC_NUM 6
#define DEF_TASK_PRIO 5
#define READ_TIMEOUT_MS 100
#define TASK_STACK_WORDS 4096

// PDM->PCM exists only on I2S0; PCM streams are forced onto port 0.
#define PCM_PORT I2S_NUM_0

// One stream per I2S controller. The whole module's mutable state lives here;
// the array index is the I2S port number, so PCM (port 0) always maps to slot 0.
struct atomic_mic_stream {
    bool in_use;
    i2s_port_t port;
    i2s_chan_handle_t rx;
    atomic_mic_fmt_t format;

    uint8_t num_mics;
    uint8_t nlanes;       // interleaved lanes per frame (== num_mics)
    uint16_t frame_bytes; // nlanes * sizeof(int16_t)
    uint8_t lane_of_mic[ATOMIC_MIC_MAX_PER_STREAM];

    int16_t *readbuf;
    size_t readbuf_bytes;

    atomic_mic_cb_t cb;
    void *user;

    TaskHandle_t task;
    volatile bool running;
    SemaphoreHandle_t exited; // given by the task as it leaves its loop
};

static struct atomic_mic_stream s_streams[ATOMIC_MIC_MAX_STREAMS];

// (line, sel) -> slot_mask bit. RIGHT is the even bit, LEFT the odd bit, matching
// i2s_pdm_slot_mask_t: line0 R/L = BIT0/BIT1, line1 = BIT2/BIT3, ...
static inline uint8_t slot_bit(const atomic_mic_desc_t *m) {
    return (uint8_t)(m->line * 2u + (m->sel == ATOMIC_MIC_SEL_LEFT ? 1u : 0u));
}

// Build the slot mask and resolve each mic's lane. Hardware emits active slots
// in ascending bit order, so a mic's lane = number of active slots below its bit.
static uint32_t build_layout(struct atomic_mic_stream *st, const atomic_mic_config_t *cfg) {
    uint32_t mask = 0;
    for (uint8_t i = 0; i < cfg->num_mics; i++) {
        mask |= (1u << slot_bit(&cfg->mics[i]));
    }
    for (uint8_t i = 0; i < cfg->num_mics; i++) {
        uint32_t below = mask & ((1u << slot_bit(&cfg->mics[i])) - 1u);
        st->lane_of_mic[i] = (uint8_t)__builtin_popcount(below);
    }
    return mask;
}

static void mic_read_task(void *arg) {
    struct atomic_mic_stream *st = (struct atomic_mic_stream *)arg;
    const TickType_t timeout = pdMS_TO_TICKS(READ_TIMEOUT_MS);
    size_t got = 0;

    while (st->running) {
        esp_err_t r = i2s_channel_read(st->rx, st->readbuf, st->readbuf_bytes, &got, timeout);
        if (r == ESP_OK) {
            size_t nframes = got / st->frame_bytes;
            if (nframes && st->cb) {
                st->cb(st->readbuf, nframes, st->nlanes, st->user);
            }
        } else if (r != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "i2s_channel_read: %s", esp_err_to_name(r));
        }
    }

    xSemaphoreGive(st->exited);
    vTaskDelete(NULL);
}

esp_err_t atomic_mic_start(const atomic_mic_config_t *cfg, atomic_mic_handle_t *out) {
    if (!cfg || cfg->num_mics == 0 || cfg->num_mics > ATOMIC_MIC_MAX_PER_STREAM) {
        return ESP_ERR_INVALID_ARG;
    }

    // Pick an I2S controller. PCM (hardware PDM->PCM) only works on I2S0.
    int idx = -1;
    if (cfg->format == ATOMIC_MIC_FMT_PCM) {
        idx = (int)PCM_PORT;
    } else {
        for (int p = 0; p < ATOMIC_MIC_MAX_STREAMS; p++) {
            if (!s_streams[p].in_use) {
                idx = p;
                break;
            }
        }
    }
    if (idx < 0 || s_streams[idx].in_use) {
        ESP_LOGE(TAG, "no free I2S controller for requested format");
        return ESP_ERR_NOT_FOUND;
    }

    struct atomic_mic_stream *st = &s_streams[idx];
    memset(st, 0, sizeof(*st));
    st->port = (i2s_port_t)idx;
    st->format = cfg->format;
    st->num_mics = cfg->num_mics;
    st->cb = cfg->on_audio;
    st->user = cfg->user;

    uint32_t slot_mask = build_layout(st, cfg);
    st->nlanes = (uint8_t)__builtin_popcount(slot_mask);
    st->frame_bytes = (uint16_t)(st->nlanes * sizeof(int16_t));

    const uint16_t read_frames = cfg->read_frames ? cfg->read_frames : DEF_READ_FRAMES;

    // 1. Allocate the channel (REGISTERED).
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(st->port, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = cfg->dma_desc_num ? cfg->dma_desc_num : DEF_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = cfg->dma_frame_num ? cfg->dma_frame_num : read_frames;

    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &st->rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(err));
        return err;
    }

    // 2. Initialize PDM RX (-> READY). Same path for PCM and RAW; only data_fmt
    //    (and the HP filter, where the SoC has one) differ.
    i2s_pdm_rx_slot_config_t slot_cfg = {
        .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
        .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
        .slot_mode = (st->nlanes == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO,
        .slot_mask = (i2s_pdm_slot_mask_t)slot_mask,
        .data_fmt =
            (cfg->format == ATOMIC_MIC_FMT_PCM) ? I2S_PDM_DATA_FMT_PCM : I2S_PDM_DATA_FMT_RAW,
    };
#if SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER
    // P4-class parts only; absent on the S3. Harmless to set under the guard.
    slot_cfg.hp_en = (cfg->format == ATOMIC_MIC_FMT_PCM);
    slot_cfg.hp_cut_off_freq_hz = 35.5f;
    slot_cfg.amplify_num = 1;
#endif

    i2s_pdm_rx_clk_config_t clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(cfg->sample_rate_hz);
    clk_cfg.dn_sample_mode = I2S_PDM_DSR_16S;

    i2s_pdm_rx_gpio_config_t gpio_cfg = {.clk = cfg->pin_clk, .invert_flags = {0}};
    for (int k = 0; k < SOC_I2S_PDM_MAX_RX_LINES; k++) {
        gpio_cfg.dins[k] = (cfg->pin_din[k] >= 0) ? cfg->pin_din[k] : GPIO_NUM_NC;
    }

    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = clk_cfg,
        .slot_cfg = slot_cfg,
        .gpio_cfg = gpio_cfg,
    };
    err = i2s_channel_init_pdm_rx_mode(st->rx, &pdm_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_pdm_rx_mode: %s", esp_err_to_name(err));
        goto fail_channel;
    }

    // Read destination: plain internal RAM (the driver memcpy's from the DMA
    // ring into it; it does not need to be DMA-capable).
    st->readbuf_bytes = (size_t)read_frames * st->frame_bytes;
    st->readbuf = heap_caps_malloc(st->readbuf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!st->readbuf) {
        ESP_LOGE(TAG, "no memory for %u-byte read buffer", (unsigned)st->readbuf_bytes);
        err = ESP_ERR_NO_MEM;
        goto fail_channel;
    }

    st->exited = xSemaphoreCreateBinary();
    if (!st->exited) {
        err = ESP_ERR_NO_MEM;
        goto fail_buf;
    }

    // 3. Enable (-> RUNNING). DMA capture starts now.
    err = i2s_channel_enable(st->rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable: %s", esp_err_to_name(err));
        goto fail_sem;
    }

    // 4. Launch the read task that drains the ring and delegates via callback.
    st->running = true;
    const uint8_t prio = cfg->task_prio ? cfg->task_prio : DEF_TASK_PRIO;
    BaseType_t created =
        (cfg->task_core >= 0)
            ? xTaskCreatePinnedToCore(mic_read_task, "mic_rx", TASK_STACK_WORDS, st, prio,
                                      &st->task, cfg->task_core)
            : xTaskCreate(mic_read_task, "mic_rx", TASK_STACK_WORDS, st, prio, &st->task);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "failed to create read task");
        st->running = false;
        err = ESP_ERR_NO_MEM;
        goto fail_enabled;
    }

    st->in_use = true;
    if (out)
        *out = st;
    ESP_LOGI(TAG, "stream on I2S%d: %u mic(s), %u lane(s), %s @ %lu Hz", idx, st->num_mics,
             st->nlanes, cfg->format == ATOMIC_MIC_FMT_PCM ? "PCM" : "RAW",
             (unsigned long)cfg->sample_rate_hz);
    return ESP_OK;

fail_enabled:
    i2s_channel_disable(st->rx);
fail_sem:
    vSemaphoreDelete(st->exited);
fail_buf:
    heap_caps_free(st->readbuf);
fail_channel:
    i2s_del_channel(st->rx);
    memset(st, 0, sizeof(*st));
    return err;
}

esp_err_t atomic_mic_stop(atomic_mic_handle_t h) {
    struct atomic_mic_stream *st = h;
    if (!st || !st->in_use) {
        return ESP_ERR_INVALID_STATE;
    }

    st->running = false;
    xSemaphoreTake(st->exited, portMAX_DELAY); // wait for the task to leave its loop
    vSemaphoreDelete(st->exited);

    i2s_channel_disable(st->rx);
    i2s_del_channel(st->rx);
    heap_caps_free(st->readbuf);
    memset(st, 0, sizeof(*st));
    return ESP_OK;
}

int atomic_mic_lane_of(atomic_mic_handle_t h, uint8_t mic_index) {
    struct atomic_mic_stream *st = h;
    if (!st || !st->in_use || mic_index >= st->num_mics) {
        return -1;
    }
    return st->lane_of_mic[mic_index];
}
