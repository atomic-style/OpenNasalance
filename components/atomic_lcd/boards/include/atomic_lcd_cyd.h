#pragma once

#include "atomic_lcd_config.h"

esp_err_t a_lcd_cyd(a_lcd_t *lcd, bool (*cb)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *));