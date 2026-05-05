// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "atomic_sd.h"
#include "atomic_bits.h"
#include "atomic_err.h"
#include "atomic_gpio.h"
#include "atomic_log.h"
#include "atomic_sd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sd_protocol_types.h"
#include "sdmmc_cmd.h"
#include <dirent.h>

#if SOC_SDMMC_IO_POWER_EXTERNAL
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#endif

static const char *TAG = "䌇";

static sdmmc_card_t *s_sd_card;
static sd_pwr_ctrl_handle_t s_sd_pwr = NULL;
static const char s_mount_point[] = "/sd";

esp_err_t atomic_sdspi_init(const a_sdspi_cfg_t *a_sdspi_cfg) {
    if (a_bits(BIT_SD_READY))
        return ESP_OK;

    sdmmc_host_t sdmmc_host_cfg = SDSPI_HOST_DEFAULT();
    sdmmc_host_cfg.slot = a_sdspi_cfg->host;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = a_sdspi_cfg->mosi_io_num,
        .miso_io_num = a_sdspi_cfg->miso_io_num,
        .sclk_io_num = a_sdspi_cfg->sclk_io_num,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 8192,
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
    };

    try(spi_bus_initialize(sdmmc_host_cfg.slot, &bus_cfg, SDSPI_DEFAULT_DMA));

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = a_sdspi_cfg->cs_io_num;
    slot_config.host_id = sdmmc_host_cfg.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false, .max_files = 5, .allocation_unit_size = 16 * 1024};

    try(esp_vfs_fat_sdspi_mount(s_mount_point, &sdmmc_host_cfg, &slot_config, &mount_config, &s_sd_card));
    a_bits_set(BIT_SD_READY);

    ESP_LOGI(TAG, "SDSPI Filesystem mounted at %s", s_mount_point);
    return ESP_OK;
}

/*
    Note: These settings are specifically for the Waveshare ESP32-P4-WIFI6 board.
    It has an on-chip LDO that controls the power to the SD card.
    This is probably an error, and will need to be changed for other boards.
*/

esp_err_t atomic_sdmmc_init(const a_sdmmc_cfg_t *a_sdmmc_cfg) {
    if (a_bits(BIT_SD_READY))
        return ESP_OK;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    host.io_voltage = 3.3f; // volts

#ifdef CONFIG_IDF_TARGET_ESP32P4
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = 4,
    };

    esp_err_t ok = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &s_sd_pwr);
    if (ok != ESP_OK)
        return ok;

    ok = sd_pwr_ctrl_set_io_voltage(s_sd_pwr, 3300); // millivolts
    if (ok != ESP_OK)
        return ok;

    host.pwr_ctrl_handle = s_sd_pwr;
#endif
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = a_sdmmc_cfg->clk_io_num;
    slot_config.cmd = a_sdmmc_cfg->cmd_io_num;
    slot_config.d0 = a_sdmmc_cfg->data_io_num;
    slot_config.d1 = a_sdmmc_cfg->data_1_io_num;
    slot_config.d2 = a_sdmmc_cfg->data_2_io_num;
    slot_config.d3 = a_sdmmc_cfg->data_3_io_num;
    slot_config.width = a_sdmmc_cfg->width;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false, .max_files = 5, .allocation_unit_size = 16 * 1024};

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(s_mount_point, &host, &slot_config, &mount_config, &s_sd_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_fat_sdmmc_mount error: %s", esp_err_to_name(ret));
        return ret;
    }

    sdmmc_card_print_info(stdout, s_sd_card);
    a_bits_set(BIT_SD_READY);
    return ESP_OK;
}

esp_err_t atomic_sd_ls(void) {
    if (!a_bits(BIT_SD_READY)) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    DIR *dir = opendir(s_mount_point);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory %s", s_mount_point);
        return ESP_FAIL;
    }

    struct dirent *entry;
    int file_count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            ESP_LOGI(TAG, "  FILE: %s", entry->d_name);
            file_count++;
        } else if (entry->d_type == DT_DIR) {
            ESP_LOGI(TAG, "  DIR:  %s", entry->d_name);
        }
    }

    closedir(dir);
    ESP_LOGI(TAG, "Total files: %d", file_count);
    return ESP_OK;
}