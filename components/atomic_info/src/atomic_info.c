// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "atomic_info.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "atomic_log.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_heap_caps_init.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "め";

/*              Flash:    Emb PSRAM:   Ext PSRAM:
- S3-Zero:       4MB         2MB           -
- Xiao S3:       8MB         8MB           -
- TDisplay:      16MB         -           8MB
- DevKit-N8R2:   8MB         2MB           -
- DevKit-N8R8:   8MB         8MB           -
- CYD:           4MB          -            -
- W5:            16MB        8MB           -
*/

void a_info_heap(void) {
    info(TAG, "a_info_heap()");

    // DEFAULT
    uint32_t caps = MALLOC_CAP_DEFAULT;
    size_t heap_total_default = heap_caps_get_total_size(caps);
    size_t heap_free_default = heap_caps_get_free_size(caps);
    size_t heap_block_default = heap_caps_get_largest_free_block(caps);
    info(TAG, "DEFAULT    total: %d, free: %d, avail: %d", heap_total_default, heap_free_default, heap_block_default);

    // 8BIT_DMA
    caps = MALLOC_CAP_DMA | MALLOC_CAP_8BIT;
    size_t heap_total_dma = heap_caps_get_total_size(caps);
    size_t heap_free_dma = heap_caps_get_free_size(caps);
    size_t heap_block_dma = heap_caps_get_largest_free_block(caps);
    info(TAG, "8BIT_DMA   total: %d, free: %d, avail: %d", heap_total_dma, heap_free_dma, heap_block_dma);

    // PSRAM/SPIRAM

    bool has_psram = esp_psram_is_initialized();
    if (!has_psram) {
        esp_err_t ok = esp_psram_init();
        if (ok != ESP_OK)
            return;
    }

    // MALLOC_CAP_SPIRAM
    caps = MALLOC_CAP_SPIRAM;
    size_t heap_total_spiram = heap_caps_get_total_size(caps);
    size_t heap_free_spiram = heap_caps_get_free_size(caps);
    size_t heap_block_spiram = heap_caps_get_largest_free_block(caps);
    info(TAG, "SPIRAM      total: %d, free: %d, avail: %d", heap_total_spiram, heap_free_spiram, heap_block_spiram);

    // SPIRAM/DMA
    caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA;
    size_t heap_total_spidma = heap_caps_get_total_size(caps);
    size_t heap_free_spidma = heap_caps_get_free_size(caps);
    size_t heap_block_spidma = heap_caps_get_largest_free_block(caps);
    info(TAG, "SPI/DMA     total: %d, free: %d, avail: %d", heap_total_spidma, heap_free_spidma, heap_block_spidma);

    info(TAG, "--------------------------------");
}

// esp_psram_extram_add_to_heap_allocator()
// esp_psram_extram_reserve_dma_pool(size_t size)

void atomic_info_print_chip(esp_chip_info_t chip_info) {
    esp_chip_model_t model = chip_info.model;
    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    switch (model) {
    case CHIP_ESP32:
        ESP_LOGI(TAG, "Chip Model: ESP32 v%d.%d", major_rev, minor_rev);
        break;
    case CHIP_ESP32S2:
        ESP_LOGI(TAG, "Chip Model: ESP32-S2 v%d.%d", major_rev, minor_rev);
        break;
    case CHIP_ESP32S3:
        ESP_LOGI(TAG, "Chip Model: ESP32-S3 v%d.%d", major_rev, minor_rev);
        break;
    case CHIP_ESP32C3:
        ESP_LOGI(TAG, "Chip Model: ESP32-C3 v%d.%d", major_rev, minor_rev);
        break;
    case CHIP_ESP32C2:
        ESP_LOGI(TAG, "Chip Model: ESP32-C2 v%d.%d", major_rev, minor_rev);
        break;
    case CHIP_ESP32C6:
        ESP_LOGI(TAG, "Chip Model: ESP32-C6 v%d.%d", major_rev, minor_rev);
        break;
    case CHIP_ESP32H2:
        ESP_LOGI(TAG, "Chip Model: ESP32-H2 v%d.%d", major_rev, minor_rev);
        break;
    case CHIP_ESP32P4:
        ESP_LOGI(TAG, "Chip Model: ESP32-P4 v%d.%d", major_rev, minor_rev);
        break;
    case CHIP_ESP32C61:
        ESP_LOGI(TAG, "Chip Model: ESP32-C61 v%d.%d", major_rev, minor_rev);
        break;
    case CHIP_ESP32C5:
        ESP_LOGI(TAG, "Chip Model: ESP32-C5 v%d.%d", major_rev, minor_rev);
        break;
    case CHIP_ESP32H21:
        ESP_LOGI(TAG, "Chip Model: ESP32-H21 v%d.%d", major_rev, minor_rev);
        break;
    case CHIP_ESP32H4:
        ESP_LOGI(TAG, "Chip Model: ESP32-H4 v%d.%d", major_rev, minor_rev);
        break;
    case CHIP_POSIX_LINUX:
        ESP_LOGI(TAG, "Chip Model: POSIX/Linux simulator v%d.%d", major_rev, minor_rev);
        break;
    default:
        ESP_LOGI(TAG, "Chip Model: Unknown (model number %d) v%d.%d", model, major_rev, minor_rev);
        break;
    }
}

void atomic_info_print_features(esp_chip_info_t chip_info) {
    ESP_LOGI(TAG, "CPU Cores: %d", chip_info.cores);
    ESP_LOGI(TAG, "Features: %s%s%s%s", (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi " : "",
             (chip_info.features & CHIP_FEATURE_BT) ? "BT " : "", (chip_info.features & CHIP_FEATURE_BLE) ? "BLE " : "",
             (chip_info.features & CHIP_FEATURE_IEEE802154) ? "802.15.4 (Zigbee/Thread) " : "");
}

void atomic_info_print_flash(esp_chip_info_t chip_info) {
    uint32_t flash_size;
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        ESP_LOGE(TAG, "Flash memory not found");
        return;
    }
    ESP_LOGI(TAG, "Flash Size: %" PRIu32 "MB %s flash", flash_size / (uint32_t)(1024 * 1024),
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
}

void atomic_info_print_psram(esp_chip_info_t chip_info) {
    if (!(chip_info.features & CHIP_FEATURE_EMB_PSRAM)) {
        return;
    }
    ESP_LOGI(TAG, "PSRAM: Embedded PSRAM detected");
}

void atomic_info_print_heap(void) {
    uint32_t free_heap = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Free Heap Size: %" PRIu32 "KB", free_heap / 1024);
}

void atomic_info_print(void) {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    ESP_LOGI(TAG, "/*------- Hardware Info --------*/");
    atomic_info_print_chip(chip_info);
    atomic_info_print_features(chip_info);
    atomic_info_print_flash(chip_info);
    atomic_info_print_psram(chip_info);
    atomic_info_print_heap();
    ESP_LOGI(TAG, "/*------------------------------*/");
    a_info_heap();
}

void atomic_info_print_task_stacks(void) {
    info(TAG, "Task Stack High Water Marks:");

    UBaseType_t num_tasks = uxTaskGetNumberOfTasks();
    TaskStatus_t *tasks = heap_caps_malloc(num_tasks * sizeof(TaskStatus_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!tasks) {
        warn(TAG, "Failed to allocate memory for task status");
        return;
    }

    uint32_t total_runtime = 0;
    num_tasks = uxTaskGetSystemState(tasks, num_tasks, &total_runtime);

    info(TAG, "Total tasks: %d", num_tasks);
    info(TAG, "%-20s %10s", "Task Name", "Min Free");

    for (UBaseType_t i = 0; i < num_tasks; i++) {
        UBaseType_t min_free = tasks[i].usStackHighWaterMark;
        UBaseType_t min_free_bytes = min_free * sizeof(StackType_t);

        info(TAG, "%-20s %10zu B", tasks[i].pcTaskName, (size_t)min_free_bytes);
    }

    heap_caps_free(tasks);

    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    info(TAG, "Internal RAM: %zu free, %zu largest block", free_internal, largest_block);

    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t largest_psram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    info(TAG, "PSRAM: %zu free, %zu largest block", free_psram, largest_psram);
}