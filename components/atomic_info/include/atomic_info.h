// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_chip_info.h"

void a_info_heap(void);
void atomic_info_print_chip(esp_chip_info_t chip_info);
void atomic_info_print_features(esp_chip_info_t chip_info);
void atomic_info_print_flash(esp_chip_info_t chip_info);
void atomic_info_print_psram(esp_chip_info_t chip_info);
void atomic_info_print_heap(void);
void atomic_info_print(void);
void atomic_info_print_task_stacks(void);

#ifdef __cplusplus
}
#endif
