#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t a_lvgl_jpeg(const char *filename);
void a_lvgl_jpeg_cleanup(void);
