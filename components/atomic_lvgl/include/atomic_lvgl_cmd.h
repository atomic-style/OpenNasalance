// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include "atomic_lvgl.h"

void atomic_lvgl_cmd_init(void);
void atomic_lvgl_cmd_deinit(void);
void atomic_lvgl_cmd_exec(const char *cmd);