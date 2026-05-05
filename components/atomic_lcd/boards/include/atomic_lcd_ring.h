#pragma once

#include "atomic_lcd_config.h"
#include "esp_err.h"

esp_err_t a_lcd_ring(a_lcd_t *lcd, bool (*cb)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *));