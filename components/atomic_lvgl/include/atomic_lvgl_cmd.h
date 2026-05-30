#pragma once

#include "atomic_lvgl.h"

void atomic_lvgl_cmd_init(void);
void atomic_lvgl_cmd_deinit(void);
void atomic_lvgl_cmd_exec(const char *cmd);