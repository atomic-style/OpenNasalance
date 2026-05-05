#pragma once

#include "atomic_lcd_config.h"
#include "esp_err.h"

typedef struct {
	uint32_t addr;
	uint8_t	 param[20];
	uint32_t len;
} lcd_cmd_t;

esp_err_t a_lcd_tdisplay(a_lcd_t *lcd, bool (*cb)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *));