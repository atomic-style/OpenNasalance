// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "atomic_info.h"
#include "atomic_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "init.h"

static const char *TAG = "󰜈";

void app_main(void) {
    notice(TAG, "app_main()");
    atomic_info_print_task_stacks();

    atomic_info_print();
    init();

    // atomic_info_print_heap();
    // atomic_info_print_task_stacks();

    uint32_t cnt = 0;
    while (1) {
        cnt++;
        if (cnt % 30 == 0) {
            debug(TAG, "tick(%u)", cnt);
        }
        /*
        if (cnt % 5 == 0) {
            warn(TAG, "---- before task creation:");
            atomic_info_print_heap();
            atomic_info_print_task_stacks();
            warn(TAG, "---------------------");
        }
        */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}