# OpenNasalance

Open source esp32-based nasometry solution for clinicians and researchers.

OpenNasalance captures dual-channel audio (one nasal mic, one oral mic), renders both spectrograms in real time on an attached display, and computes a rolling **nasalance** value (`100 × Eₙ / (Eₙ + E_o)` over a configurable passband).

Recordings are written to SD as paired 16-bit mono WAV files for downstream analysis. 

---

## Recommended Hardware

| Component | Part |
|---|---|
| MCU / Display | ESP32S3 with display |
| Microphones | 2× MEMS microphones (TDM I²S, shared CLK/WS) |
| Storage | microSD (FAT, ≤ 32 GB) |

Pin assignments live in [`main/include/config.h`](main/include/config.h).

## Prototype Hardware

| Component | Part |
|---|---|
| MCU / Display | QDtech 2.8" **ES3N28P** (ESP32-S3R8, 16 MB flash, 8 MB PSRAM, 320×240 IPS, FT6336G touch, onboard SD) |
| Microphones | 2× MEMS-441 (TDM I²S, shared CLK/WS), to be replaced with T5837 |

---

## Repository layout

```
main/                Project entry point & high-level wiring
  include/config.h     ← all build-time options (start here)
  src/init.c           Bring-up sequence: NVS → SD → display → touch → mic → UI
  src/init_display.c   atomic_lcd panel setup
  src/init_touch.c     atomic_touch + LVGL pointer wiring
  src/nasometer.c      FFT, spectrograms, nasalance, WAV recorder, LVGL UI

components/          Reusable building blocks
  atomic_lcd/          Panel driver (ILI9341 family, board-specific init)
  atomic_lvgl/         LVGL bridge (display flush, FS, jpeg, command shim)
  atomic_touch/        FT6336G I²C driver, exposes shared bus via atomic_touch_get_bus()
  atomic_mic/          Dual-mic TDM I²S capture (a_mic_441_*)
  atomic_sd/           SDMMC/SDSPI mount
  atomic_nvs/          Thin NVS wrapper
  atomic_events/       FreeRTOS event group helpers
  atomic_bme280/       Experimental: BME280 pressure-trace UI (off by default)
  atomic_bits/, atomic_gpio/, atomic_info/, atomic_util/   Misc utilities

docs/
  research/           Reference papers / protocol standards

default_16MB.csv     Partition table
sdkconfig.defaults   Project-wide IDF config defaults
```

---

## Build & flash

Built against **ESP-IDF 6.0.1**.

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem1101 flash monitor
```

---

## Configuration

Everything is centralised in `main/include/config.h`. The most likely things to edit:

| Section | What it controls |
|---|---|
| **Hardware target** | Pick a board variant. Currently only `ESP_ES3N28P` is wired up. |
| **Feature toggles** | `ENABLE_LCD`, `ENABLE_LVGL`, `ENABLE_TOUCH`, `ENABLE_SD`, `ENABLE_MIC`, `ENABLE_BME280`. Comment a flag to drop the subsystem. |
| **Hardware pins** | Per-board GPIO map. Modify if you re-wire something. |
| **Spectrograph** | `SPECTRO_FFT_SIZE`, `SPECTRO_HOP`, `SPECTRO_MIN_HZ` / `MAX_HZ`, dB floor/ceiling. |
| **Nasalance** | `NASALANCE_BAND_LOW_HZ` / `HIGH_HZ`, `NASALANCE_WINDOW_SEC`, `NASALANCE_NASAL_MIC` (which TDM channel is the nasal mic). |

The BME280 experiment has its own knobs in
[`components/atomic_bme280/include/pressure.h`](components/atomic_bme280/include/pressure.h)
(`PRESSURE_SAMPLE_HZ`, `PRESSURE_DELTA_MIN_PA`, `PRESSURE_DELTA_MAX_PA`,
`PRESSURE_STATS_WINDOW_SEC`).

---

## Reference reading

In `docs/research/`:

- **Oren et al., 2019** — high-speed nasopharyngoscopy of the velopharyngeal valve (primary collaborator team)
- **Siriwardena et al., 2024** — speaker-independent speech inversion for VP port constriction
- **LSU nasalance protocol standardization** — clinical reference for passband / measurement methodology
- **applsci-09-03040** & **2505.23339v1** — additional methodology

---

## License

OpenNasalance is licensed under the **GNU General Public License v3.0 or
later** (`SPDX-License-Identifier: GPL-3.0-or-later`). The full license text
is in [`LICENSE.md`](LICENSE.md); third-party attribution is in
[`NOTICE`](NOTICE).

In short: you are free to use, modify, and redistribute OpenNasalance —
including in commercial settings — provided that any derivative work is also
distributed under the GPL with full source. If you need OpenNasalance under
proprietary terms (e.g. embedded inside a closed-source medical device),
contact **scott@atomic.style** for commercial licensing.

## Contributing

Contributions are welcome. Please read [`CONTRIBUTING.md`](CONTRIBUTING.md)
before opening a pull request.

To enable us to keep OpenNasalance available under GPL while also offering
commercial licenses to organisations that need them, we ask contributors to
sign one of the following Contributor License Agreements:

- [`CLA-INDIVIDUAL.md`](CLA-INDIVIDUAL.md) — for individual contributors
- [`CLA-ENTITY.md`](CLA-ENTITY.md) — for companies, universities, and other
  legal entities

Both CLAs let you keep copyright on your work and bind Atomic Style, LLC to keep the Project available under GPL (or another FSF/OSI-approved license) in perpetuity. See [`CONTRIBUTING.md`](CONTRIBUTING.md) for the signing flow.

[![CLA assistant](https://cla-assistant.io/readme/badge/atomic-style/OpenNasalance)](https://cla-assistant.io/atomic-style/OpenNasalance) 

---