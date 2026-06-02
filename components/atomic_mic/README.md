# Atomic Mic

You traded the Bluesmobile for this?
No, for a microphone.

## config examples for different configurations

### 1) One mono mic on line 0 (SEL low)
```c
  atomic_mic_config_t c = {
      .format = ATOMIC_MIC_FMT_PCM, .sample_rate_hz = 24000,
      .pin_clk = CLK, .pin_din = { D0, -1, -1, -1 },
      .num_mics = 1, .mics = {{ .line = 0, .sel = ATOMIC_MIC_SEL_LEFT }},
      .on_audio = on_audio, .task_core = 1,
  };
  // -> 1 lane: frames = [m0, m0, m0, ...]
```

### 2) Stereo, two mics on the SAME data line, hardware L & R
```c
  .num_mics = 2,
  .mics = {{ .line = 0, .sel = ATOMIC_MIC_SEL_LEFT },
           { .line = 0, .sel = ATOMIC_MIC_SEL_RIGHT }},
  .pin_din = { D0, -1, -1, -1 },          // one shared DATA pin
  // -> ONE handle, 2 lanes interleaved: [L, R, L, R, ...]
```

### 3) Two mics, one per DATA line
```c
  .num_mics = 2,
  .mics = {{ .line = 0, .sel = ATOMIC_MIC_SEL_LEFT },
           { .line = 1, .sel = ATOMIC_MIC_SEL_LEFT }},
  .pin_din = { D0, D1, -1, -1 },
  // -> ONE handle, 2 lanes interleaved: [line0, line1, ...]
```

## Consume

```c
  static void on_audio(const int16_t *frames, size_t nframes, uint8_t nlanes, void *user) {
      int la = atomic_mic_lane_of(h, 0);  // resolve once, cache these
      int lb = atomic_mic_lane_of(h, 1);
      for (size_t f = 0; f < nframes; f++) {
          int16_t a = atomic_mic_sample(frames, nlanes, f, la);
          int16_t b = (nlanes > 1) ? atomic_mic_sample(frames, nlanes, f, lb) : 0;
          // push to spectrogram / nasalance / WAV ringbuffer here
      }
  }
```