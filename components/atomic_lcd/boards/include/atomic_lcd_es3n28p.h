#pragma once

#include "atomic_lcd_config.h"
#include "esp_err.h"

// ShenZhen QDtech ES3C28P / ES3N28P 2.8" IPS ESP32-S3 Display Module.
// ILI9341V controller, 240x320, 4-line SPI. Reset line is tied to CHIP_PU
// (shared with ESP32-S3 reset), so the panel has no separate reset GPIO.
esp_err_t a_lcd_es3n28p(a_lcd_t *lcd,
                        bool (*cb)(esp_lcd_panel_io_handle_t,
                                   esp_lcd_panel_io_event_data_t *, void *));
