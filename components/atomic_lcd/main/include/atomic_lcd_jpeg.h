#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

esp_err_t a_jpeg_setup(void);
esp_err_t a_jpeg_14(const char *filename);

#ifdef __cplusplus
}
#endif
