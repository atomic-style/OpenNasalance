#include "a_mic.h"

#include "atomic_err.h"
#include "atomic_log.h"
#include "driver/i2s_common.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "㇐";

static i2s_chan_handle_t s_rx_chan = NULL;   // INMP441 master, reads data_1
static i2s_chan_handle_t s_rx_chan_2 = NULL; // INMP441 slave, reads data_2
static i2s_chan_handle_t s_t5837_chan = NULL;
static int s_t5837_gain = 1;        // software gain for the T5837 read path
static bool s_t5837_stereo = false; // set in a_mic_t5837_init from cfg->stereo

// In stereo mode each mic gets its own PDM RX data line (LINE0 + LINE1 on
// I2S0). DMA fills sample slots in ascending slot-mask bit order, so with
// slot_mask = LINE0_SLOT_LEFT(BIT1) | LINE1_SLOT_LEFT(BIT3), LINE0 lands at
// chunk[2k] and LINE1 at chunk[2k+1]. If on the bench mic1 audio appears on
// buf2 (or vice-versa), swap these two:
#define T5837_STEREO_IDX_MIC1 0
#define T5837_STEREO_IDX_MIC2 1

esp_err_t a_mic_init(a_mic_config_t *a_mic_config) {
    debug(TAG, "a_mic_init pin_clk=%d pin_data=%d", a_mic_config->pin_clk, a_mic_config->pin_data);
    return ESP_OK;
}

esp_err_t a_mic_441_init(a_mic_441_config_t *cfg) {
    notice(TAG, "init INMP441  bclk=GPIO%d ws=GPIO%d din1=GPIO%d din2=GPIO%d  %dHz", cfg->pin_clk,
           cfg->pin_ws, cfg->pin_data_1, cfg->pin_data_2, cfg->sample_rate);

    i2s_chan_config_t chan_cfg_m = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    try(i2s_new_channel(&chan_cfg_m, NULL, &s_rx_chan));

    i2s_chan_config_t chan_cfg_s = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_SLAVE);
    try(i2s_new_channel(&chan_cfg_s, NULL, &s_rx_chan_2));

    i2s_std_slot_config_t slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO);
    slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    i2s_std_config_t std_cfg_m = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(cfg->sample_rate),
        .slot_cfg = slot_cfg,
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = cfg->pin_clk,
                .ws = cfg->pin_ws,
                .dout = I2S_GPIO_UNUSED,
                .din = cfg->pin_data_1,
                .invert_flags = {0},
            },
    };

    i2s_std_config_t std_cfg_s = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(cfg->sample_rate),
        .slot_cfg = slot_cfg,
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = cfg->pin_clk,
                .ws = cfg->pin_ws,
                .dout = I2S_GPIO_UNUSED,
                .din = cfg->pin_data_2,
                .invert_flags = {0},
            },
    };

    try(i2s_channel_init_std_mode(s_rx_chan, &std_cfg_m));
    try(i2s_channel_init_std_mode(s_rx_chan_2, &std_cfg_s));
    try(i2s_channel_enable(s_rx_chan));
    try(i2s_channel_enable(s_rx_chan_2));

    notice(TAG, "INMP441 dual running");
    return ESP_OK;
}

esp_err_t a_mic_441_read_dual(int32_t *buf1, int32_t *buf2, size_t frames, int timeout_ms) {
    if (s_rx_chan == NULL || s_rx_chan_2 == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    TickType_t to = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    size_t want = frames * sizeof(int32_t);
    size_t got1 = 0, got2 = 0;

    if (buf1) {
        esp_err_t r = i2s_channel_read(s_rx_chan, buf1, want, &got1, to);
        if (r != ESP_OK)
            return r;
        if (got1 != want)
            return ESP_ERR_TIMEOUT;
    }
    if (buf2) {
        esp_err_t r = i2s_channel_read(s_rx_chan_2, buf2, want, &got2, to);
        if (r != ESP_OK)
            return r;
        if (got2 != want)
            return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

// --- T5837 (PDM, mono or stereo) --------------------------------------------
//
// The ESP32 drives CLK and samples DATA. Mode is set by cfg->stereo:
//
//   stereo=false (mono): one mic on pin_data_1, PDM MONO mode, LEFT slot of
//                        LINE0. Both buf1 and buf2 receive the same data
//                        (mono fan-out).
//
//   stereo=true:         two mics, each on its OWN DATA pin (pin_data_1 ->
//                        PDM RX LINE0, pin_data_2 -> LINE1) — the S3 PDM RX
//                        block has 4 independent data lines on I2S0. Both
//                        mics wired with SELECT=GND so each fills its line's
//                        LEFT slot. No shared DATA, no tri-state contention.
//                        read_dual de-interleaves buf1 = mic1, buf2 = mic2.
//
// PCM samples come out 16-bit. read_dual places each in the HIGH half of an
// int32 word (sample << 16) so the nasometer's spectrograph (>>8, /2^23) and
// its WAV down-convert (>>16) both read the correct magnitude with no change.
//
// The S3's PDM->PCM block has neither a hardware high-pass nor an amplifier
// (both are P4-only), and the MEMS element is quiet at unity, so read_dual runs
// a one-pole DC blocker (per output lane) followed by an integer software gain
// (with clipping). Gain raises level/visibility, not SNR — it scales signal
// and noise together.

esp_err_t a_mic_t5837_init(a_mic_t5837_config_t *cfg) {
    s_t5837_gain = (cfg->gain > 0) ? cfg->gain : 1;
    s_t5837_stereo = cfg->stereo;
    if (s_t5837_stereo) {
        notice(TAG, "init T5837 (PDM stereo)  clk=GPIO%d din1=GPIO%d din2=GPIO%d  %dHz  gain=%dx",
               cfg->pin_clk, cfg->pin_data_1, cfg->pin_data_2, cfg->sample_rate, s_t5837_gain);
    } else {
        notice(TAG, "init T5837 (PDM mono)  clk=GPIO%d din=GPIO%d  %dHz  gain=%dx", cfg->pin_clk,
               cfg->pin_data_1, cfg->sample_rate, s_t5837_gain);
    }

    // PDM RX is hardware-locked to I2S0 on the ESP32-S3 — the driver rejects an
    // I2S1 handle with ESP_ERR_INVALID_ARG ("PDM is only supported on I2S0").
    // Pin the controller explicitly instead of relying on I2S_NUM_AUTO's
    // allocation order.
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    if (cfg->buf_samples > 0)
        chan_cfg.dma_frame_num = cfg->buf_samples;
    try(i2s_new_channel(&chan_cfg, NULL, &s_t5837_chan));

    // 16-bit PCM (S3 supports PDM2PCM; no hardware HP — read_dual does that).
    // Mono: default macro selects LINE0 LEFT slot (= our single mic, SELECT=GND).
    // Stereo: SLOT_MODE_STEREO with LINE0+LINE1 LEFT, one mic per data line.
    //
    // The MONO/STEREO branch is split out (rather than expressed as a runtime
    // ternary on the macro's mono_or_stereo arg) because the macro itself does
    // (mono_or_stereo == MONO) ? LEFT : BOTH internally, and feeding it a
    // runtime ternary trips -Werror=int-in-bool-context.
    i2s_pdm_rx_slot_config_t slot_cfg;
    if (s_t5837_stereo) {
        slot_cfg = (i2s_pdm_rx_slot_config_t)I2S_PDM_RX_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
        slot_cfg.slot_mask = I2S_PDM_RX_LINE0_SLOT_LEFT | I2S_PDM_RX_LINE1_SLOT_LEFT;
    } else {
        slot_cfg = (i2s_pdm_rx_slot_config_t)I2S_PDM_RX_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    }
    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(cfg->sample_rate),
        .slot_cfg = slot_cfg,
        .gpio_cfg = {.clk = cfg->pin_clk, .invert_flags = {0}},
    };
    // Use the dins[] member of the gpio_cfg union so we can wire one pin per
    // mic in stereo mode (LINE0 + LINE1); mono just uses dins[0].
    pdm_cfg.gpio_cfg.dins[0] = cfg->pin_data_1;
    if (s_t5837_stereo)
        pdm_cfg.gpio_cfg.dins[1] = cfg->pin_data_2;

    // The T5837 selects its operating mode by PDM-clock band, with a DEAD GAP
    // between Low-Power (400-800 kHz) and High-Quality (2.0-3.7 MHz). DSR_16 sets
    // the PDM clock = sample_rate * 128, so 24 kHz -> 3.072 MHz (High-Quality).
    // DSR_8 would give 1.536 MHz — in the dead gap → pure static. The true PCM
    // output rate is NOT simply sample_rate here; spectro_task measures it at
    // runtime and uses it for the WAV header.
    pdm_cfg.clk_cfg.dn_sample_mode = I2S_PDM_DSR_16S;
    uint32_t pdm_clk_hz = (uint32_t)cfg->sample_rate * 128u;
    notice(TAG, "PDM clock = %u Hz (T5837 HQ band 2.0-3.7 MHz)", (unsigned)pdm_clk_hz);

    try(i2s_channel_init_pdm_rx_mode(s_t5837_chan, &pdm_cfg));
    try(i2s_channel_enable(s_t5837_chan));

    notice(TAG, "T5837 %s running", s_t5837_stereo ? "stereo" : "mono");
    return ESP_OK;
}

// One-pole DC blocker (sub-40 Hz) + integer software gain + clip → int32 with
// the 16-bit sample in the high half. The S3 PDM2PCM path has no hardware HP
// or amp, so un-removed DC offset would eat headroom and clip asymmetrically
// once gain is applied. Per-lane state because stereo mode runs two
// independent streams.
static int32_t t5837_process_sample(int16_t raw, float *dc_x1, float *dc_y1) {
    float x = (float)raw;
    float y = x - *dc_x1 + 0.995f * *dc_y1;
    *dc_x1 = x;
    *dc_y1 = y;
    int32_t s = (int32_t)lrintf(y * (float)s_t5837_gain);
    if (s > 32767)
        s = 32767;
    if (s < -32768)
        s = -32768;
    return s << 16;
}

// Blocking read of `frames` PCM samples per output buffer.
//   Mono mode  : reads N int16s, fans the same data to buf1 and buf2.
//   Stereo mode: reads 2N int16s (interleaved per the IDF/TDK mapping noted
//                near the file top), de-interleaves so buf1 = mic1 (the
//                SELECT=GND side) and buf2 = mic2 (SELECT=VDD).
// Either buf1 or buf2 may be NULL.
esp_err_t a_mic_t5837_read_dual(int32_t *buf1, int32_t *buf2, size_t frames, int timeout_ms) {
    if (s_t5837_chan == NULL)
        return ESP_ERR_INVALID_STATE;
    TickType_t to = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    // [0] = mic1 lane (buf1), [1] = mic2 lane (buf2). In mono mode only [0]
    // moves; [1] is initialized but unused.
    static float s_dc_x1[2] = {0.0f, 0.0f};
    static float s_dc_y1[2] = {0.0f, 0.0f};

    if (!s_t5837_stereo) {
        int16_t chunk[128];
        size_t done = 0;
        while (done < frames) {
            size_t want = frames - done;
            if (want > 128)
                want = 128;
            size_t got = 0;
            esp_err_t r = i2s_channel_read(s_t5837_chan, chunk, want * sizeof(int16_t), &got, to);
            if (r != ESP_OK)
                return r;
            size_t got_frames = got / sizeof(int16_t);
            if (got_frames == 0)
                return ESP_ERR_TIMEOUT;
            for (size_t i = 0; i < got_frames; i++) {
                int32_t v = t5837_process_sample(chunk[i], &s_dc_x1[0], &s_dc_y1[0]);
                if (buf1)
                    buf1[done + i] = v;
                if (buf2)
                    buf2[done + i] = v;
            }
            done += got_frames;
        }
    } else {
        // Stereo: each PCM frame is two int16s (mic1, mic2 in the order set
        // by T5837_STEREO_IDX_MIC{1,2} near the file top). chunk holds 64
        // stereo frames per I2S read.
        int16_t chunk[128]; // = 64 stereo frames
        size_t done = 0;
        while (done < frames) {
            size_t want_frames = frames - done;
            if (want_frames > 64)
                want_frames = 64;
            size_t want_bytes = want_frames * 2 * sizeof(int16_t);
            size_t got = 0;
            esp_err_t r = i2s_channel_read(s_t5837_chan, chunk, want_bytes, &got, to);
            if (r != ESP_OK)
                return r;
            size_t got_frames = got / (2 * sizeof(int16_t));
            if (got_frames == 0)
                return ESP_ERR_TIMEOUT;
            for (size_t i = 0; i < got_frames; i++) {
                int16_t raw1 = chunk[i * 2 + T5837_STEREO_IDX_MIC1];
                int16_t raw2 = chunk[i * 2 + T5837_STEREO_IDX_MIC2];
                int32_t v1 = t5837_process_sample(raw1, &s_dc_x1[0], &s_dc_y1[0]);
                int32_t v2 = t5837_process_sample(raw2, &s_dc_x1[1], &s_dc_y1[1]);
                if (buf1)
                    buf1[done + i] = v1;
                if (buf2)
                    buf2[done + i] = v2;
            }
            done += got_frames;
        }
    }

    // No logging here: this runs on the real-time audio task. Console writes
    // block until the host drains them while `idf.py monitor` is attached
    // (UART0 + the USB-Serial-JTAG console), which would stall the I2S read and
    // drop samples — heard as continuous static only while the monitor is open.
    return ESP_OK;
}
