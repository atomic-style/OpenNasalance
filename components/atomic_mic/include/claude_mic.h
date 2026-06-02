#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

// ESP32-S3: I2S0 PDM RX supports up to 4 data lines; the chip has 2 I2S
// controllers (only I2S0 has the hardware PDM->PCM filter).
#define ATOMIC_MIC_MAX_PER_STREAM 4
#define ATOMIC_MIC_MAX_STREAMS 2

// Sample/data format for a whole stream.
typedef enum {
    ATOMIC_MIC_FMT_PCM = 0, // hardware PDM->PCM (I2S0 only); read 16-bit PCM
    ATOMIC_MIC_FMT_RAW = 1, // raw PDM bitstream; decode to PCM in software
} atomic_mic_fmt_t;

// Which clock-phase slot a mic occupies on its data line. Fixed in hardware by
// the mic's SEL / L-R strap pin.
typedef enum {
    ATOMIC_MIC_SEL_LEFT = 0,  // SEL pulled low
    ATOMIC_MIC_SEL_RIGHT = 1, // SEL pulled high
} atomic_mic_sel_t;

// One physical microphone within a stream.
typedef struct {
    uint8_t line;         // PDM data line 0..3; indexes atomic_mic_config_t.pin_din[]
    atomic_mic_sel_t sel; // clock-phase slot on that line
} atomic_mic_desc_t;

// Opaque per-stream handle returned by atomic_mic_start().
typedef struct atomic_mic_stream *atomic_mic_handle_t;

// Audio delivery callback, invoked from the stream's read task.
//   frames : interleaved int16 samples, one lane per active mic
//   nframes: frames (samples per lane) in this block
//   nlanes : number of interleaved lanes (== number of mics in the stream)
//   user   : opaque pointer from the config
// Lanes are emitted in hardware order (ascending line, RIGHT before LEFT). Use
// atomic_mic_lane_of() to map a logical mic index to its lane, then
// atomic_mic_sample() to pull it out. Keep this callback light: it runs in the
// real-time read task. Hand heavy work (FFT, SD writes) off to another task.
typedef void (*atomic_mic_cb_t)(const int16_t *frames, size_t nframes, uint8_t nlanes, void *user);

typedef struct {
    atomic_mic_fmt_t format;
    uint32_t sample_rate_hz; // PCM: ~16-48kHz. RAW: PDM clock, e.g. 3072000
    int pin_clk;
    int pin_din[ATOMIC_MIC_MAX_PER_STREAM]; // per-line data pins; <0 if unused

    uint8_t num_mics;
    atomic_mic_desc_t mics[ATOMIC_MIC_MAX_PER_STREAM];

    atomic_mic_cb_t on_audio; // delivery callback (may be NULL)
    void *user;               // passed back to the callback

    // Optional tuning; leave 0 for library defaults.
    uint16_t read_frames;   // frames per read / callback block (default 256)
    uint8_t dma_desc_num;   // DMA ring depth (default 6)
    uint16_t dma_frame_num; // frames per DMA buffer (default = read_frames)
    int task_core;          // pin read task to core; <0 = no affinity
    uint8_t task_prio;      // read task priority (default 5)
} atomic_mic_config_t;

// Start a stream: allocates an I2S controller, configures PDM RX, allocates the
// read buffer, and launches the read task. Returns a handle in *out on success.
esp_err_t atomic_mic_start(const atomic_mic_config_t *cfg, atomic_mic_handle_t *out);

// Stop a stream: joins the read task, tears down the channel, frees the buffer.
esp_err_t atomic_mic_stop(atomic_mic_handle_t h);

// Lane index (position in the interleaved frame) for a logical mic, or -1 if
// invalid. Resolve once after start; it does not change while the stream runs.
int atomic_mic_lane_of(atomic_mic_handle_t h, uint8_t mic_index);

// Pull one lane's sample out of an interleaved block delivered to the callback.
static inline int16_t atomic_mic_sample(const int16_t *frames, uint8_t nlanes, size_t frame,
                                        uint8_t lane) {
    return frames[frame * nlanes + lane];
}
