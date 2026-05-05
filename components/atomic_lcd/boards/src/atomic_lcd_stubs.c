#include "atomic_lcd_config.h"
#include "atomic_lcd_w5.h"
#include "atomic_lcd_cyd.h"
#include "atomic_lcd_ring.h"
#include "esp_err.h"

esp_err_t a_lcd_w5(a_lcd_t *lcd, bool (*cb)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *)) {
    (void)lcd; (void)cb;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t a_lcd_cyd(a_lcd_t *lcd, bool (*cb)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *)) {
    (void)lcd; (void)cb;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t a_lcd_ring(a_lcd_t *lcd, bool (*cb)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *)) {
    (void)lcd; (void)cb;
    return ESP_ERR_NOT_SUPPORTED;
}
