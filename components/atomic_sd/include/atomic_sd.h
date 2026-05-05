// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include "driver/sdspi_host.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a_sdspi_cfg_s {
    spi_host_device_t host;
    uint8_t mosi_io_num;
    uint8_t miso_io_num;
    uint8_t sclk_io_num;
    uint8_t cs_io_num;
} a_sdspi_cfg_t;

typedef struct a_sdmmc_cfg_s {
    uint8_t width;
    uint8_t clk_io_num;
    uint8_t cmd_io_num;
    uint8_t data_io_num;
    uint8_t data_1_io_num;
    uint8_t data_2_io_num;
    uint8_t data_3_io_num;
} a_sdmmc_cfg_t;

esp_err_t atomic_sdspi_init(const a_sdspi_cfg_t *a_sdspi_cfg);
esp_err_t atomic_sdmmc_init(const a_sdmmc_cfg_t *a_sdmmc_cfg);
esp_err_t atomic_sd_ls(void);

#ifdef __cplusplus
}
#endif
