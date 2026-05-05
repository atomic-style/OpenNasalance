// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "nasometer.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "a_mic.h"
#include "atomic_bits.h"
#include "atomic_err.h"
#include "atomic_log.h"
#include "atomic_nvs.h"
#include "config.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "㊣";

#define N SPECTRO_FFT_SIZE
#define HOP SPECTRO_HOP

// Display layout (320x240 landscape). 18px top text, 18px nasalance readout,
// 36px button row at the bottom, two 80px spectrograms between, 1px rules.
//
//  0 .. 17    top text      (Nasometer)
//  y=18       rule
//  19 ..98    spectro A     (mic 1 / top)
//  y=99       rule
//  100..179   spectro B     (mic 2 / bottom)
//  y=180      rule
//  181..199   bottom text   (Nasalance / Time)
//  y=200      rule
//  201..239   button row    (Start / Stop / Reset)
#define DISPLAY_W 320
#define DISPLAY_H 240
#define TEXT_TOP_H 18
#define TEXT_BOT_H 19
#define BUTTON_ROW_H 39
#define SPECTRO_H 80
#define SPECTRO_W DISPLAY_W

#define RULE1_Y (TEXT_TOP_H)                                  // 18
#define SPECTRO_A_Y (RULE1_Y + 1)                             // 19
#define RULE2_Y (SPECTRO_A_Y + SPECTRO_H)                     // 99
#define SPECTRO_B_Y (RULE2_Y + 1)                             // 100
#define RULE3_Y (SPECTRO_B_Y + SPECTRO_H)                     // 180
#define TEXT_BOT_Y (RULE3_Y + 1)                              // 181
#define RULE4_Y (TEXT_BOT_Y + TEXT_BOT_H)                     // 200
#define BUTTON_ROW_Y (RULE4_Y + 1)                            // 201

#define BYTES_PER_PX 2 // RGB565

// FFT bin range mapped to the vertical axis of each spectrogram
#define MAX_BIN ((int)((int64_t)SPECTRO_MAX_HZ * N / MIC_SAMPLE_RATE))
#define MIN_BIN ((int)((int64_t)SPECTRO_MIN_HZ * N / MIC_SAMPLE_RATE))

// Nasalance passband (runtime, overridable via Options screen). Defaults from config.h.
static int s_nasal_lo_hz = NASALANCE_BAND_LOW_HZ;
static int s_nasal_hi_hz = NASALANCE_BAND_HIGH_HZ;

static inline int hz_to_bin(int hz) {
    return (int)((int64_t)hz * N / MIC_SAMPLE_RATE);
}

// Rolling window size in FFT frames: (fs / hop) * seconds, +1 margin
#define NASAL_FRAMES ((MIC_SAMPLE_RATE * NASALANCE_WINDOW_SEC) / HOP + 1)

static lv_obj_t *s_canvas_top = NULL;
static lv_obj_t *s_canvas_bot = NULL;
static lv_obj_t *s_lbl_top = NULL;
static lv_obj_t *s_lbl_bot = NULL;
static lv_obj_t *s_btn_start = NULL;
static lv_obj_t *s_btn_stop = NULL;
static lv_obj_t *s_btn_reset = NULL;
static lv_obj_t *s_btn_opt = NULL;
static lv_obj_t *s_btn_mode_s = NULL;
static lv_obj_t *s_btn_mode_a = NULL;
static lv_obj_t *s_btn_mode_b = NULL;
static uint16_t *s_buf_top = NULL;
static uint16_t *s_buf_bot = NULL;

// LVGL screens
static lv_obj_t *s_scr_main = NULL;
static lv_obj_t *s_scr_opt  = NULL;

// Options-screen widgets
static lv_obj_t *s_lbl_lo = NULL;
static lv_obj_t *s_lbl_hi = NULL;
static lv_obj_t *s_keypad = NULL;
static lv_obj_t *s_opt_btn_ok     = NULL;
static lv_obj_t *s_opt_btn_cancel = NULL;
static lv_obj_t *s_opt_btn_reset  = NULL;
static int       s_opt_lo_hz = 0;
static int       s_opt_hi_hz = 0;
static int       s_opt_focus = 0;  // 0 = lo, 1 = hi

typedef enum {
    REC_STOPPED = 0,
    REC_RUNNING = 1,
} rec_state_t;

static volatile rec_state_t s_rec_state = REC_STOPPED;
static int64_t s_run_start_us = 0;   // esp_timer_get_time() at last Start
static double  s_run_elapsed_us = 0; // accumulated time across Start/Stop cycles

// FFT working set
static float    s_window[N];
static float    s_tw_cos[N / 2];
static float    s_tw_sin[N / 2];
static uint16_t s_bitrev[N];

// Colormap 256 -> RGB565
static uint16_t s_colormap[256];

// Amplitude trace state (per-canvas previous y so we can draw a vertical
// segment connecting consecutive frames, avoiding gaps when y jumps).
#define AMP_TRACE_DB_FLOOR (-60.0f)
#define AMP_TRACE_DB_CEIL   (0.0f)
static int s_amp_y_top_prev = SPECTRO_H - 1;
static int s_amp_y_bot_prev = SPECTRO_H - 1;

// View mode toggled by the S/A/B buttons in the top-left.
typedef enum {
    VIEW_SPECTRO   = 0,  // spectrogram only
    VIEW_AMPLITUDE = 1,  // filled amplitude only
    VIEW_BOTH      = 2,  // spectrogram + amplitude line
} view_mode_t;
static volatile view_mode_t s_view_mode = VIEW_BOTH;

// Rolling nasalance buffers
static float  s_nasal_ring[NASAL_FRAMES];
static float  s_oral_ring[NASAL_FRAMES];
static int    s_ring_head = 0;
static double s_nasal_sum = 0.0;
static double s_oral_sum = 0.0;

// ---------- helpers ----------

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// 5-stop gradient: black → deep blue → magenta → orange → yellow
static void colormap_init(void) {
    const float stops[6][3] = {
        {0.00f, 0.00f, 0.00f},
        {0.12f, 0.00f, 0.38f},
        {0.50f, 0.00f, 0.50f},
        {0.90f, 0.30f, 0.10f},
        {1.00f, 0.85f, 0.25f},
        {1.00f, 1.00f, 0.85f},
    };
    const int nstops = 6;
    for (int i = 0; i < 256; i++) {
        float t = (float)i / 255.0f;
        float pos = t * (nstops - 1);
        int a = (int)pos;
        if (a >= nstops - 1) a = nstops - 2;
        float f = pos - a;
        float r = stops[a][0] + f * (stops[a + 1][0] - stops[a][0]);
        float g = stops[a][1] + f * (stops[a + 1][1] - stops[a][1]);
        float b = stops[a][2] + f * (stops[a + 1][2] - stops[a][2]);
        s_colormap[i] = rgb565((uint8_t)(r * 255), (uint8_t)(g * 255), (uint8_t)(b * 255));
    }
}

static void window_init(void) {
    for (int n = 0; n < N; n++) {
        s_window[n] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * n / (N - 1)));
    }
}

static int ilog2_i(int v) {
    int r = 0;
    while (v > 1) { v >>= 1; r++; }
    return r;
}

static void bitrev_init(void) {
    int bits = ilog2_i(N);
    for (int i = 0; i < N; i++) {
        unsigned x = (unsigned)i;
        unsigned r = 0;
        for (int b = 0; b < bits; b++) {
            r = (r << 1) | (x & 1);
            x >>= 1;
        }
        s_bitrev[i] = (uint16_t)r;
    }
}

static void twiddles_init(void) {
    for (int k = 0; k < N / 2; k++) {
        float a = -2.0f * (float)M_PI * k / N;
        s_tw_cos[k] = cosf(a);
        s_tw_sin[k] = sinf(a);
    }
}

// In-place radix-2 Cooley-Tukey FFT.
static void fft_forward(float *re, float *im) {
    for (int i = 0; i < N; i++) {
        int j = s_bitrev[i];
        if (j > i) {
            float tr = re[i], ti = im[i];
            re[i] = re[j]; im[i] = im[j];
            re[j] = tr;    im[j] = ti;
        }
    }
    for (int size = 2; size <= N; size <<= 1) {
        int half = size >> 1;
        int step = N / size;
        for (int i = 0; i < N; i += size) {
            int twi = 0;
            for (int j = 0; j < half; j++) {
                float cr = s_tw_cos[twi], ci = s_tw_sin[twi];
                float a_re = re[i + j], a_im = im[i + j];
                float b_re = re[i + j + half], b_im = im[i + j + half];
                float t_re = b_re * cr - b_im * ci;
                float t_im = b_re * ci + b_im * cr;
                re[i + j]        = a_re + t_re;
                im[i + j]        = a_im + t_im;
                re[i + j + half] = a_re - t_re;
                im[i + j + half] = a_im - t_im;
                twi += step;
            }
        }
    }
}

// Scroll one canvas left by 1 column, write magnitude column at x = SPECTRO_W-1.
static void render_column(uint16_t *buf, const float *re, const float *im) {
    for (int y = 0; y < SPECTRO_H; y++) {
        uint16_t *row = buf + y * SPECTRO_W;
        memmove(row, row + 1, (SPECTRO_W - 1) * sizeof(uint16_t));
    }

    const int bin_span = MAX_BIN - MIN_BIN;
    const int x = SPECTRO_W - 1;
    const float db_floor = (float)SPECTRO_DB_FLOOR;
    const float db_range = (float)(SPECTRO_DB_CEIL - SPECTRO_DB_FLOOR);

    for (int y = 0; y < SPECTRO_H; y++) {
        float fpos = (float)(SPECTRO_H - 1 - y) / (float)(SPECTRO_H - 1);
        int bin = MIN_BIN + (int)(fpos * bin_span + 0.5f);
        if (bin < 1) bin = 1;
        if (bin >= N / 2) bin = N / 2 - 1;

        float r = re[bin], i = im[bin];
        float mag2 = r * r + i * i;
        float db = (mag2 > 1e-20f) ? 10.0f * log10f(mag2) - 20.0f * log10f((float)N / 2.0f)
                                    : -200.0f;
        float t = (db - db_floor) / db_range;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        int idx = (int)(t * 255.0f);
        buf[y * SPECTRO_W + x] = s_colormap[idx];
    }
}

static int amp_db_to_y(float db) {
    float t = (db - AMP_TRACE_DB_FLOOR) / (AMP_TRACE_DB_CEIL - AMP_TRACE_DB_FLOOR);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    int y = SPECTRO_H - 1 - (int)(t * (SPECTRO_H - 1));
    if (y < 0) y = 0;
    if (y >= SPECTRO_H) y = SPECTRO_H - 1;
    return y;
}

// Paint a colored segment in the rightmost column connecting prev_y to new_y.
// Must run AFTER render_column so the spectrogram fill at column SPECTRO_W-1
// doesn't overwrite the trace pixels.
static void render_amp_trace(uint16_t *buf, int *prev_y, int new_y, uint16_t color) {
    const int x = SPECTRO_W - 1;
    int y0 = *prev_y, y1 = new_y;
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (y0 < 0) y0 = 0;
    if (y1 >= SPECTRO_H) y1 = SPECTRO_H - 1;
    for (int y = y0; y <= y1; y++) {
        buf[y * SPECTRO_W + x] = color;
    }
    *prev_y = new_y;
}

// Filled amplitude column: scrolls left like render_column, then fills the new
// rightmost column with `color` from amp_y down to the bottom (and black above).
static void render_filled_column(uint16_t *buf, int amp_y, uint16_t color) {
    for (int y = 0; y < SPECTRO_H; y++) {
        uint16_t *row = buf + y * SPECTRO_W;
        memmove(row, row + 1, (SPECTRO_W - 1) * sizeof(uint16_t));
    }
    const int x = SPECTRO_W - 1;
    if (amp_y < 0) amp_y = 0;
    if (amp_y >= SPECTRO_H) amp_y = SPECTRO_H - 1;
    for (int y = 0; y < amp_y; y++) {
        buf[y * SPECTRO_W + x] = 0x0000;
    }
    for (int y = amp_y; y < SPECTRO_H; y++) {
        buf[y * SPECTRO_W + x] = color;
    }
}

// Sum of |X[k]|^2 over the nasalance passband bins (inclusive).
static float band_energy(const float *re, const float *im) {
    float acc = 0.0f;
    int lo = hz_to_bin(s_nasal_lo_hz), hi = hz_to_bin(s_nasal_hi_hz);
    if (lo < 1) lo = 1;
    if (hi >= N / 2) hi = N / 2 - 1;
    for (int k = lo; k <= hi; k++) {
        acc += re[k] * re[k] + im[k] * im[k];
    }
    return acc;
}

static void push_nasalance(float nasal, float oral) {
    s_nasal_sum += nasal - s_nasal_ring[s_ring_head];
    s_oral_sum  += oral  - s_oral_ring[s_ring_head];
    s_nasal_ring[s_ring_head] = nasal;
    s_oral_ring[s_ring_head]  = oral;
    s_ring_head = (s_ring_head + 1) % NASAL_FRAMES;
}

static void reset_nasalance(void) {
    memset(s_nasal_ring, 0, sizeof(s_nasal_ring));
    memset(s_oral_ring, 0, sizeof(s_oral_ring));
    s_nasal_sum = 0.0;
    s_oral_sum = 0.0;
    s_ring_head = 0;
}

static double elapsed_seconds(void) {
    double us = s_run_elapsed_us;
    if (s_rec_state == REC_RUNNING) {
        us += (double)(esp_timer_get_time() - s_run_start_us);
    }
    return us / 1e6;
}

// ---------- NVS persistence (Options) ----------

#define NVS_KEY_NASAL_LO "nasal_lo"
#define NVS_KEY_NASAL_HI "nasal_hi"

static void nvs_load_options(void) {
    uint32_t v;
    if (a_nvs_get_u32(NVS_KEY_NASAL_LO, &v) == ESP_OK) s_nasal_lo_hz = (int)v;
    if (a_nvs_get_u32(NVS_KEY_NASAL_HI, &v) == ESP_OK) s_nasal_hi_hz = (int)v;
    notice(TAG, "options loaded: %dHz..%dHz", s_nasal_lo_hz, s_nasal_hi_hz);
}

static void nvs_save_options(void) {
    a_nvs_set_u32(NVS_KEY_NASAL_LO, (uint32_t)s_nasal_lo_hz);
    a_nvs_set_u32(NVS_KEY_NASAL_HI, (uint32_t)s_nasal_hi_hz);
    notice(TAG, "options saved: %dHz..%dHz", s_nasal_lo_hz, s_nasal_hi_hz);
}

// ---------- WAV PCM 16-bit mono writer ----------
//
// No RTC on this board, so filenames use a scanned sequence number rather
// than wall-clock date/time:  /sd/Nas_0001_mic1.wav, /sd/Nas_0001_mic2.wav.
// Write requests are flagged from the LVGL thread (button callbacks) and
// serviced on the audio thread (spectro_task), which owns the FILE handles.

#define WAV_DIR "/sd"

static FILE   *s_wav_a = NULL;
static FILE   *s_wav_b = NULL;
static uint32_t s_wav_samples = 0;
static volatile bool s_wav_request_open  = false;
static volatile bool s_wav_request_close = false;

static void wav_put_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void wav_put_u16_le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
}

static void wav_write_header(FILE *f, uint32_t total_samples) {
    uint8_t  hdr[44];
    uint32_t data_bytes = total_samples * 2; // 16-bit mono
    uint32_t riff_size  = 36 + data_bytes;
    uint32_t sample_rate = MIC_SAMPLE_RATE;
    uint16_t num_ch = 1, bits = 16;
    uint32_t byte_rate = sample_rate * num_ch * (bits / 8);
    uint16_t block_align = num_ch * (bits / 8);

    memcpy(hdr +  0, "RIFF", 4);
    wav_put_u32_le(hdr +  4, riff_size);
    memcpy(hdr +  8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    wav_put_u32_le(hdr + 16, 16);   // fmt chunk size
    wav_put_u16_le(hdr + 20, 1);    // PCM
    wav_put_u16_le(hdr + 22, num_ch);
    wav_put_u32_le(hdr + 24, sample_rate);
    wav_put_u32_le(hdr + 28, byte_rate);
    wav_put_u16_le(hdr + 32, block_align);
    wav_put_u16_le(hdr + 34, bits);
    memcpy(hdr + 36, "data", 4);
    wav_put_u32_le(hdr + 40, data_bytes);

    fseek(f, 0, SEEK_SET);
    fwrite(hdr, 1, 44, f);
}

static int wav_next_sequence(void) {
    DIR *d = opendir(WAV_DIR);
    if (!d) return 1;
    int max_n = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "Nas_", 4) != 0) continue;
        int n = atoi(de->d_name + 4);
        if (n > max_n) max_n = n;
    }
    closedir(d);
    return max_n + 1;
}

static void wav_open_pair(void) {
    if (!a_bits(BIT_SD_READY)) {
        warn(TAG, "WAV: SD not ready, skipping recording");
        return;
    }
    int seq = wav_next_sequence();
    char path_a[64], path_b[64];
    snprintf(path_a, sizeof(path_a), "%s/Nas_%04d_mic1.wav", WAV_DIR, seq);
    snprintf(path_b, sizeof(path_b), "%s/Nas_%04d_mic2.wav", WAV_DIR, seq);
    s_wav_a = fopen(path_a, "wb");
    s_wav_b = fopen(path_b, "wb");
    if (!s_wav_a || !s_wav_b) {
        warn(TAG, "WAV: fopen failed (seq %d)", seq);
        if (s_wav_a) { fclose(s_wav_a); s_wav_a = NULL; }
        if (s_wav_b) { fclose(s_wav_b); s_wav_b = NULL; }
        return;
    }
    // Reserve a blank 44-byte header; patched on close.
    uint8_t blank[44] = {0};
    fwrite(blank, 1, 44, s_wav_a);
    fwrite(blank, 1, 44, s_wav_b);
    s_wav_samples = 0;
    notice(TAG, "WAV: opened seq %04d", seq);
}

static void wav_close_pair(void) {
    if (s_wav_a) {
        wav_write_header(s_wav_a, s_wav_samples);
        fclose(s_wav_a);
        s_wav_a = NULL;
    }
    if (s_wav_b) {
        wav_write_header(s_wav_b, s_wav_samples);
        fclose(s_wav_b);
        s_wav_b = NULL;
    }
    notice(TAG, "WAV: closed (%u samples)", (unsigned)s_wav_samples);
}

static void wav_write_frames(const int32_t *s1, const int32_t *s2, size_t n) {
    if (!s_wav_a || !s_wav_b) return;
    int16_t buf_a[128];
    int16_t buf_b[128];
    size_t i = 0;
    while (i < n) {
        size_t chunk = n - i;
        if (chunk > 128) chunk = 128;
        for (size_t k = 0; k < chunk; k++) {
            int32_t v1 = s1[i + k] >> 16;
            int32_t v2 = s2[i + k] >> 16;
            if (v1 >  32767) v1 =  32767;
            if (v1 < -32768) v1 = -32768;
            if (v2 >  32767) v2 =  32767;
            if (v2 < -32768) v2 = -32768;
            buf_a[k] = (int16_t)v1;
            buf_b[k] = (int16_t)v2;
        }
        fwrite(buf_a, sizeof(int16_t), chunk, s_wav_a);
        fwrite(buf_b, sizeof(int16_t), chunk, s_wav_b);
        i += chunk;
    }
    s_wav_samples += (uint32_t)n;
}

// Bright colors mean "this action is available"; dim means "current state,
// or n/a". We also tint the Stop button when recording so it reads as the
// primary action.
#define COL_START_ON  lv_color_make(0x1e, 0x9e, 0x1e)
#define COL_START_OFF lv_color_make(0x0e, 0x3d, 0x0e)
#define COL_STOP_ON   lv_color_make(0x9e, 0x1e, 0x1e)
#define COL_STOP_OFF  lv_color_make(0x3d, 0x0e, 0x0e)
#define COL_RESET     lv_color_make(0x4a, 0x4a, 0x4a)

static void refresh_status_label(int nasalance, double t_sec) {
    if (!s_lbl_bot) return;
    int t_tenths = (int)(t_sec * 10.0 + 0.5);
    const char *status = (s_rec_state == REC_RUNNING) ? "Recording" : "Stopped";
    lv_label_set_text_fmt(s_lbl_bot, "Nasalance: %2d   Time: %3d.%d   [%s]",
                           nasalance, t_tenths / 10, t_tenths % 10, status);
}

static void refresh_button_styles(void) {
    bool running = (s_rec_state == REC_RUNNING);
    if (s_btn_start) {
        lv_obj_set_style_bg_color(s_btn_start, running ? COL_START_OFF : COL_START_ON, 0);
    }
    if (s_btn_stop) {
        lv_obj_set_style_bg_color(s_btn_stop, running ? COL_STOP_ON : COL_STOP_OFF, 0);
    }
}

static void btn_start_cb(lv_event_t *e) {
    (void)e;
    if (s_rec_state == REC_RUNNING) return;
    s_run_start_us = esp_timer_get_time();
    s_rec_state = REC_RUNNING;
    s_wav_request_open = true;
    refresh_button_styles();
    notice(TAG, "Start pressed");
}

static void btn_stop_cb(lv_event_t *e) {
    (void)e;
    if (s_rec_state == REC_STOPPED) return;
    s_run_elapsed_us += (double)(esp_timer_get_time() - s_run_start_us);
    s_rec_state = REC_STOPPED;
    s_wav_request_close = true;
    refresh_button_styles();
    double denom = s_nasal_sum + s_oral_sum;
    int nasalance = (denom > 1e-12) ? (int)(100.0 * s_nasal_sum / denom + 0.5) : 0;
    refresh_status_label(nasalance, s_run_elapsed_us / 1e6);
    notice(TAG, "Stop pressed  elapsed=%.1fs  nasalance=%d",
           s_run_elapsed_us / 1e6, nasalance);
}

static void refresh_mode_button_styles(void) {
    lv_obj_t *btns[3]      = { s_btn_mode_s, s_btn_mode_a, s_btn_mode_b };
    view_mode_t modes[3]   = { VIEW_SPECTRO, VIEW_AMPLITUDE, VIEW_BOTH };
    lv_color_t on_color    = lv_color_make(0x80, 0x80, 0x80);
    lv_color_t off_color   = lv_color_make(0x20, 0x20, 0x20);
    for (int i = 0; i < 3; i++) {
        if (!btns[i]) continue;
        lv_obj_set_style_bg_color(btns[i], (s_view_mode == modes[i]) ? on_color : off_color, 0);
    }
}

static void btn_mode_s_cb(lv_event_t *e) {
    (void)e;
    s_view_mode = VIEW_SPECTRO;
    refresh_mode_button_styles();
}

static void btn_mode_a_cb(lv_event_t *e) {
    (void)e;
    s_view_mode = VIEW_AMPLITUDE;
    refresh_mode_button_styles();
}

static void btn_mode_b_cb(lv_event_t *e) {
    (void)e;
    s_view_mode = VIEW_BOTH;
    refresh_mode_button_styles();
}

static void btn_reset_cb(lv_event_t *e) {
    (void)e;
    s_rec_state = REC_STOPPED;
    s_run_elapsed_us = 0;
    s_run_start_us = 0;
    s_wav_request_close = true; // close any open file
    reset_nasalance();
    refresh_button_styles();
    refresh_status_label(0, 0.0);
    notice(TAG, "Reset pressed");
}

static void show_main_screen(void);
static void show_options_screen(void);
static void build_options_ui(void);

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int x, int y, int w, int h,
                             lv_color_t bg, lv_event_cb_t cb) {
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);
    return btn;
}

// ---------- Options screen ----------

static void update_field_label(lv_obj_t *lbl, int hz) {
    if (!lbl) return;
    lv_label_set_text_fmt(lbl, "%d Hz", hz);
}

// Gate the right-side OK/Cancel/Reset buttons so the keypad's tap area can't
// fall through onto a screen-action button.
static void set_opt_buttons_enabled(bool en) {
    lv_obj_t *btns[] = { s_opt_btn_ok, s_opt_btn_cancel, s_opt_btn_reset };
    for (int i = 0; i < 3; i++) {
        if (!btns[i]) continue;
        if (en) {
            lv_obj_remove_state(btns[i], LV_STATE_DISABLED);
            lv_obj_add_flag(btns[i], LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_add_state(btns[i], LV_STATE_DISABLED);
            lv_obj_remove_flag(btns[i], LV_OBJ_FLAG_CLICKABLE);
        }
    }
}

static void show_keypad(void) {
    if (!s_keypad) return;
    lv_obj_remove_flag(s_keypad, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_keypad);
    set_opt_buttons_enabled(false);
}

static void hide_keypad(void) {
    if (s_keypad) lv_obj_add_flag(s_keypad, LV_OBJ_FLAG_HIDDEN);
    set_opt_buttons_enabled(true);
}

static void btn_opt_cb(lv_event_t *e) {
    (void)e;
    s_opt_lo_hz = s_nasal_lo_hz;
    s_opt_hi_hz = s_nasal_hi_hz;
    show_options_screen();
}

static void opt_lo_cb(lv_event_t *e) {
    (void)e;
    s_opt_focus = 0;
    show_keypad();
}

static void opt_hi_cb(lv_event_t *e) {
    (void)e;
    s_opt_focus = 1;
    show_keypad();
}

static void keypad_cb(lv_event_t *e) {
    lv_obj_t *bm = (lv_obj_t *)lv_event_get_target(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(bm);
    const char *txt = lv_buttonmatrix_get_button_text(bm, id);
    if (!txt) return;
    int *target = (s_opt_focus == 0) ? &s_opt_lo_hz : &s_opt_hi_hz;
    if (strcmp(txt, "<") == 0) {
        *target /= 10;
    } else if (strcmp(txt, "OK") == 0) {
        hide_keypad();
        return;
    } else if (txt[0] >= '0' && txt[0] <= '9') {
        int new_v = (*target) * 10 + (txt[0] - '0');
        if (new_v <= 9999) *target = new_v;
    }
    update_field_label((s_opt_focus == 0) ? s_lbl_lo : s_lbl_hi, *target);
}

static void opt_ok_cb(lv_event_t *e) {
    (void)e;
    hide_keypad();
    if (s_opt_lo_hz < 1 || s_opt_hi_hz <= s_opt_lo_hz || s_opt_hi_hz >= SPECTRO_MAX_HZ) {
        warn(TAG, "options invalid: lo=%d hi=%d (range 1..%d, hi>lo)",
             s_opt_lo_hz, s_opt_hi_hz, SPECTRO_MAX_HZ);
        return;
    }
    s_nasal_lo_hz = s_opt_lo_hz;
    s_nasal_hi_hz = s_opt_hi_hz;
    nvs_save_options();
    show_main_screen();
}

static void opt_cancel_cb(lv_event_t *e) {
    (void)e;
    hide_keypad();
    show_main_screen();
}

static void opt_reset_cb(lv_event_t *e) {
    (void)e;
    s_opt_lo_hz = NASALANCE_BAND_LOW_HZ;
    s_opt_hi_hz = NASALANCE_BAND_HIGH_HZ;
    update_field_label(s_lbl_lo, s_opt_lo_hz);
    update_field_label(s_lbl_hi, s_opt_hi_hz);
    hide_keypad();
}

static lv_obj_t *make_field(lv_obj_t *parent, const char *cap, int x, int y, int w, int h,
                            lv_event_cb_t cb) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, lv_color_make(0x20, 0x20, 0x20), 0);
    lv_obj_set_style_radius(card, 4, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 4, 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cap_lbl = lv_label_create(card);
    lv_label_set_text(cap_lbl, cap);
    lv_obj_set_style_text_color(cap_lbl, lv_color_make(0xa0, 0xa0, 0xa0), 0);
    lv_obj_set_pos(cap_lbl, 4, 2);

    lv_obj_t *val_lbl = lv_label_create(card);
    lv_obj_set_style_text_color(val_lbl, lv_color_white(), 0);
    lv_label_set_text(val_lbl, "—");
    lv_obj_set_pos(val_lbl, 4, h - 20);
    return val_lbl;
}

static void build_options_ui(void) {
    s_scr_opt = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_opt, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_scr_opt, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_scr_opt, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_scr_opt, 0, 0);
    lv_obj_set_style_border_width(s_scr_opt, 0, 0);

    lv_obj_t *title = lv_label_create(s_scr_opt);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "Options");
    lv_obj_set_size(title, DISPLAY_W, TEXT_TOP_H);
    lv_obj_set_pos(title, 0, 2);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    // Left half: Freq Lo / Freq Hi cards
    s_lbl_lo = make_field(s_scr_opt, "Freq Low",  8, 30, 144, 50, opt_lo_cb);
    s_lbl_hi = make_field(s_scr_opt, "Freq High", 8, 90, 144, 50, opt_hi_cb);

    // Right half: OK / Cancel / Reset
    const int rx = 168, rw = 144, rh = 35;
    s_opt_btn_ok     = make_button(s_scr_opt, "OK",     rx, 30,  rw, rh, COL_START_ON,  opt_ok_cb);
    s_opt_btn_cancel = make_button(s_scr_opt, "Cancel", rx, 75,  rw, rh, COL_RESET,     opt_cancel_cb);
    s_opt_btn_reset  = make_button(s_scr_opt, "Reset",  rx, 120, rw, rh, COL_STOP_OFF,  opt_reset_cb);

    // Telephone keypad — overlays the right half (covers OK/Cancel/Reset, which
    // are also disabled while it's open so a stray tap can't fall through).
    static const char *kp_map[] = {
        "1", "2", "3", "\n",
        "4", "5", "6", "\n",
        "7", "8", "9", "\n",
        "<", "0", "OK", ""
    };
    s_keypad = lv_buttonmatrix_create(s_scr_opt);
    lv_buttonmatrix_set_map(s_keypad, kp_map);
    lv_obj_set_size(s_keypad, 156, 200);
    lv_obj_set_pos(s_keypad, 160, 30);
    lv_obj_set_style_bg_color(s_keypad, lv_color_make(0x10, 0x10, 0x10), 0);
    lv_obj_set_style_border_width(s_keypad, 0, 0);
    lv_obj_set_style_pad_all(s_keypad, 2, 0);
    lv_obj_add_event_cb(s_keypad, keypad_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_flag(s_keypad, LV_OBJ_FLAG_HIDDEN);
}

static void show_options_screen(void) {
    lv_lock();
    if (!s_scr_opt) build_options_ui();
    update_field_label(s_lbl_lo, s_opt_lo_hz);
    update_field_label(s_lbl_hi, s_opt_hi_hz);
    hide_keypad();
    lv_screen_load(s_scr_opt);
    lv_unlock();
}

static void show_main_screen(void) {
    lv_lock();
    if (s_scr_main) lv_screen_load(s_scr_main);
    lv_unlock();
}

static void add_rule(lv_obj_t *scr, int y) {
    lv_obj_t *r = lv_obj_create(scr);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, DISPLAY_W, 1);
    lv_obj_set_pos(r, 0, y);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, lv_color_make(96, 96, 96), 0);
}

static void build_ui(void) {
    lv_lock();
    lv_obj_t *scr = lv_screen_active();
    s_scr_main = scr;
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);

    // Top title
    s_lbl_top = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_top, lv_color_white(), 0);
    lv_label_set_text(s_lbl_top, "Nasometer");
    lv_obj_set_size(s_lbl_top, DISPLAY_W, TEXT_TOP_H);
    lv_obj_set_pos(s_lbl_top, 0, 2);
    lv_obj_set_style_text_align(s_lbl_top, LV_TEXT_ALIGN_CENTER, 0);

    // Top spectrogram canvas
    s_canvas_top = lv_canvas_create(scr);
    lv_canvas_set_buffer(s_canvas_top, s_buf_top, SPECTRO_W, SPECTRO_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas_top, 0, SPECTRO_A_Y);
    lv_canvas_fill_bg(s_canvas_top, lv_color_black(), LV_OPA_COVER);

    // Bottom spectrogram canvas
    s_canvas_bot = lv_canvas_create(scr);
    lv_canvas_set_buffer(s_canvas_bot, s_buf_bot, SPECTRO_W, SPECTRO_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas_bot, 0, SPECTRO_B_Y);
    lv_canvas_fill_bg(s_canvas_bot, lv_color_black(), LV_OPA_COVER);

    // Rules
    add_rule(scr, RULE1_Y);
    add_rule(scr, RULE2_Y);
    add_rule(scr, RULE3_Y);
    add_rule(scr, RULE4_Y);

    // Bottom readout
    s_lbl_bot = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_bot, lv_color_white(), 0);
    lv_label_set_text(s_lbl_bot, "Nasalance: 0   Time: 0.0   [Stopped]");
    lv_obj_set_size(s_lbl_bot, DISPLAY_W, TEXT_BOT_H);
    lv_obj_set_pos(s_lbl_bot, 4, TEXT_BOT_Y);

    // Mode buttons (S / A / B) — squares stacked along the top-left, overlaying
    // the upper spectrogram. Toggle which view is rendered.
    const int mb_size = 27;
    const int mb_gap  = 7;
    const int mb_x    = 2;
    s_btn_mode_s = make_button(scr, "S", mb_x, 2,                       mb_size, mb_size, COL_RESET, btn_mode_s_cb);
    s_btn_mode_a = make_button(scr, "A", mb_x, 2 + (mb_size + mb_gap),  mb_size, mb_size, COL_RESET, btn_mode_a_cb);
    s_btn_mode_b = make_button(scr, "B", mb_x, 2 + (mb_size + mb_gap)*2, mb_size, mb_size, COL_RESET, btn_mode_b_cb);
    refresh_mode_button_styles();

    // Button row: Start | Stop | Reset (72w each) + Opt (small, 64w)
    const int btn_h = 35;
    const int btn_y = BUTTON_ROW_Y + 2;
    s_btn_start = make_button(scr, "Start",   8, btn_y, 72, btn_h, COL_START_ON, btn_start_cb);
    s_btn_stop  = make_button(scr, "Stop",   88, btn_y, 72, btn_h, COL_STOP_OFF, btn_stop_cb);
    s_btn_reset = make_button(scr, "Reset", 168, btn_y, 72, btn_h, COL_RESET,    btn_reset_cb);
    s_btn_opt   = make_button(scr, "Opt",   248, btn_y, 64, btn_h, COL_RESET,    btn_opt_cb);
    refresh_button_styles();
    refresh_status_label(0, 0.0);

    lv_unlock();
}

static void spectro_task(void *arg) {
    notice(TAG, "spectro task  N=%d hop=%d cols=%d  bins spectro=[%d..%d]  nasal=[%dHz..%dHz]  win=%ds (%d frames)",
           N, HOP, SPECTRO_W, MIN_BIN, MAX_BIN, s_nasal_lo_hz, s_nasal_hi_hz,
           NASALANCE_WINDOW_SEC, NASAL_FRAMES);

    int32_t *s1 = heap_caps_malloc(N * sizeof(int32_t), MALLOC_CAP_INTERNAL);
    int32_t *s2 = heap_caps_malloc(N * sizeof(int32_t), MALLOC_CAP_INTERNAL);
    float *re1 = heap_caps_malloc(N * sizeof(float), MALLOC_CAP_INTERNAL);
    float *im1 = heap_caps_malloc(N * sizeof(float), MALLOC_CAP_INTERNAL);
    float *re2 = heap_caps_malloc(N * sizeof(float), MALLOC_CAP_INTERNAL);
    float *im2 = heap_caps_malloc(N * sizeof(float), MALLOC_CAP_INTERNAL);

    if (!s1 || !s2 || !re1 || !im1 || !re2 || !im2) {
        err(TAG, "failed to allocate FFT scratch");
        vTaskDelete(NULL);
        return;
    }

    esp_err_t r = a_mic_441_read_dual(s1, s2, N, 2000);
    if (r != ESP_OK) {
        err(TAG, "initial mic read failed: %s", esp_err_to_name(r));
        vTaskDelete(NULL);
        return;
    }

    int ui_tick = 0;

    while (1) {
        // Service WAV open/close requests on this thread (we own the FILE handles).
        if (s_wav_request_open) {
            s_wav_request_open = false;
            wav_open_pair();
        }
        if (s_wav_request_close) {
            s_wav_request_close = false;
            wav_close_pair();
        }

        int64_t sum1 = 0, sum2 = 0;
        for (int n = 0; n < N; n++) {
            sum1 += s1[n] >> 8;
            sum2 += s2[n] >> 8;
        }
        float mean1 = (float)(sum1 / N);
        float mean2 = (float)(sum2 / N);

        double sumsq1 = 0.0, sumsq2 = 0.0;
        for (int n = 0; n < N; n++) {
            float x1 = ((float)(s1[n] >> 8) - mean1) * (1.0f / 8388608.0f);
            float x2 = ((float)(s2[n] >> 8) - mean2) * (1.0f / 8388608.0f);
            sumsq1 += x1 * x1;
            sumsq2 += x2 * x2;
            re1[n] = x1 * s_window[n];
            im1[n] = 0.0f;
            re2[n] = x2 * s_window[n];
            im2[n] = 0.0f;
        }
        float rms1 = sqrtf((float)(sumsq1 / N));
        float rms2 = sqrtf((float)(sumsq2 / N));
        float amp_db1 = (rms1 > 1e-9f) ? 20.0f * log10f(rms1) : -200.0f;
        float amp_db2 = (rms2 > 1e-9f) ? 20.0f * log10f(rms2) : -200.0f;
        int amp_y1 = amp_db_to_y(amp_db1);
        int amp_y2 = amp_db_to_y(amp_db2);

        fft_forward(re1, im1);
        fft_forward(re2, im2);

        // Per-frame band energies. Mic assignment is configurable so the board
        // can be wired either way. Accumulation is gated on the Start/Stop UI —
        // the spectrograms keep scrolling either way so the user can preview.
        float e1 = band_energy(re1, im1);
        float e2 = band_energy(re2, im2);
        float e_nasal = (NASALANCE_NASAL_MIC == 1) ? e1 : e2;
        float e_oral  = (NASALANCE_NASAL_MIC == 1) ? e2 : e1;
        if (s_rec_state == REC_RUNNING) {
            push_nasalance(e_nasal, e_oral);
        }

        const uint16_t COL_TRACE_TOP = 0x07FF; // cyan
        const uint16_t COL_TRACE_BOT = 0x07E0; // green

        view_mode_t mode = s_view_mode;

        lv_lock();
        if (mode == VIEW_AMPLITUDE) {
            render_filled_column(s_buf_top, amp_y1, COL_TRACE_TOP);
            render_filled_column(s_buf_bot, amp_y2, COL_TRACE_BOT);
            s_amp_y_top_prev = amp_y1;
            s_amp_y_bot_prev = amp_y2;
        } else {
            render_column(s_buf_top, re1, im1);
            render_column(s_buf_bot, re2, im2);
            if (mode == VIEW_BOTH) {
                render_amp_trace(s_buf_top, &s_amp_y_top_prev, amp_y1, COL_TRACE_TOP);
                render_amp_trace(s_buf_bot, &s_amp_y_bot_prev, amp_y2, COL_TRACE_BOT);
            } else { // VIEW_SPECTRO — no overlay, but keep prev_y in sync
                s_amp_y_top_prev = amp_y1;
                s_amp_y_bot_prev = amp_y2;
            }
        }
        lv_obj_invalidate(s_canvas_top);
        lv_obj_invalidate(s_canvas_bot);

        // ~10 Hz label refresh. Only update while recording — once stopped,
        // the displayed nasalance/time is the locked-in result set by btn_stop_cb.
        if (++ui_tick >= 5) {
            ui_tick = 0;
            if (s_rec_state == REC_RUNNING) {
                double denom = s_nasal_sum + s_oral_sum;
                int nasalance = (denom > 1e-12) ? (int)(100.0 * s_nasal_sum / denom + 0.5) : 0;
                refresh_status_label(nasalance, elapsed_seconds());
            }
        }
        lv_unlock();

        if (HOP < N) {
            memmove(s1, s1 + HOP, (N - HOP) * sizeof(int32_t));
            memmove(s2, s2 + HOP, (N - HOP) * sizeof(int32_t));
            r = a_mic_441_read_dual(s1 + (N - HOP), s2 + (N - HOP), HOP, 2000);
            if (r == ESP_OK && s_rec_state == REC_RUNNING) {
                wav_write_frames(s1 + (N - HOP), s2 + (N - HOP), HOP);
            }
        } else {
            r = a_mic_441_read_dual(s1, s2, N, 2000);
            if (r == ESP_OK && s_rec_state == REC_RUNNING) {
                wav_write_frames(s1, s2, N);
            }
        }
        if (r != ESP_OK) {
            warn(TAG, "mic read: %s", esp_err_to_name(r));
        }
    }
}

esp_err_t nasometer_init(void) {
    notice(TAG, "nasometer_init()");

    uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    s_buf_top = heap_caps_malloc(SPECTRO_W * SPECTRO_H * BYTES_PER_PX, caps);
    s_buf_bot = heap_caps_malloc(SPECTRO_W * SPECTRO_H * BYTES_PER_PX, caps);
    if (!s_buf_top || !s_buf_bot) {
        warn(TAG, "PSRAM canvas alloc failed; trying internal");
        if (!s_buf_top) s_buf_top = heap_caps_malloc(SPECTRO_W * SPECTRO_H * BYTES_PER_PX, MALLOC_CAP_8BIT);
        if (!s_buf_bot) s_buf_bot = heap_caps_malloc(SPECTRO_W * SPECTRO_H * BYTES_PER_PX, MALLOC_CAP_8BIT);
        if (!s_buf_top || !s_buf_bot) {
            err(TAG, "canvas allocation failed");
            return ESP_ERR_NO_MEM;
        }
    }
    memset(s_buf_top, 0, SPECTRO_W * SPECTRO_H * BYTES_PER_PX);
    memset(s_buf_bot, 0, SPECTRO_W * SPECTRO_H * BYTES_PER_PX);

    colormap_init();
    window_init();
    bitrev_init();
    twiddles_init();

    nvs_load_options();
    build_ui();

    BaseType_t r = xTaskCreatePinnedToCore(spectro_task, "nasometer", 8192, NULL, 4, NULL, 0);
    if (r != pdPASS) {
        err(TAG, "failed to create spectro task");
        return ESP_FAIL;
    }

    return ESP_OK;
}
