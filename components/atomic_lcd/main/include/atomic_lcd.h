#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atomic_lcd_config.h"
#include "esp_err.h"
#include "esp_lcd_types.h"

a_lcd_t	 *a_lcd_get(void);
bool	  a_lcd_cb(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event_data, void *user_ctx);
esp_err_t a_lcd_init(a_board_id_t id);
