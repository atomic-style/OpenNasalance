#include "atomic_lcd_cyd.h"

#include "atomic_err.h"
#include "atomic_gpio.h"
#include "atomic_lcd_config.h"
#include "atomic_log.h"
#include "driver/spi_common.h"
#include "esp_intr_types.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "cyd";

#define CYD_H_RES 320
#define CYD_V_RES 240
#define CYD_BPP 16
#define CYD_RGB_ELE_ORDER LCD_BGR
#define CYD_MAX_TRANSFER_SZ 4092

static const a_lcd_cfg_t a_lcd_cfg = {
		.h_res = CYD_H_RES,
		.v_res = CYD_V_RES,
		.bpp   = CYD_BPP,
		.rgb   = CYD_RGB_ELE_ORDER,
        .max_transfer_sz = CYD_MAX_TRANSFER_SZ,
};

static const a_lcd_gpio_cfg_t a_lcd_gpio_cfg = {
		.cs	   = 15,
		.sclk  = 14,
		.mosi  = 13,
		.miso  = 12,
		.data2 = -1,
		.data3 = -1,
		.data4 = -1,
		.data5 = -1,
		.data6 = -1,
		.data7 = -1,
		.rst   = -1,
		.dc	   = 2,
		.bl	   = 21,
		.te	   = -1,
};

static const spi_bus_config_t a_spi_bus_cfg = {
		.sclk_io_num	 = 14,
		.mosi_io_num	 = 13,
		.miso_io_num	 = 12,
		.quadwp_io_num	 = -1,
		.quadhd_io_num	 = -1,
		.data4_io_num	 = -1,
		.data5_io_num	 = -1,
		.data6_io_num	 = -1,
		.data7_io_num	 = -1,
		.max_transfer_sz = (uint32_t) CYD_MAX_TRANSFER_SZ,
};

static esp_lcd_panel_io_spi_config_t a_spi_io_cfg = {
		.cs_gpio_num		 = 15,
		.dc_gpio_num		 = 2,
		.spi_mode			 = 0,
		.pclk_hz			 = 20 * 1000 * 1000,
		.trans_queue_depth	 = 10,
		.on_color_trans_done = NULL,
		.user_ctx			 = NULL,
		.lcd_cmd_bits		 = 8,
		.lcd_param_bits		 = 8,
		.flags =
				{
						.quad_mode = false,
				},
};

static esp_lcd_panel_dev_config_t a_dev_cfg = {
		.reset_gpio_num = 2,
		.rgb_ele_order	= CYD_RGB_ELE_ORDER,
		.bits_per_pixel = 16,
};

esp_err_t a_lcd_cyd(a_lcd_t *lcd, bool (*cb)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *))
{
	info(TAG, "a_lcd_cyd()");
	esp_lcd_panel_handle_t	  panel		= NULL;
	esp_lcd_panel_io_handle_t io_handle = NULL;

	info(TAG, "initializing spi bus host: %d, channel: %d", LCD_SPI_HOST, SPI_DMA_CH_AUTO);
	esp_err_t ok = spi_bus_initialize(LCD_SPI_HOST, &a_spi_bus_cfg, SPI_DMA_CH_AUTO);
	if (ok != ESP_OK) {
		warn(TAG, "a_lcd_cyd() failed to initialize spi bus");
		return ok;
	}
	info(TAG, "spi bus initialized");

	a_spi_io_cfg.on_color_trans_done = cb;

	ok = esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &a_spi_io_cfg, &io_handle);
	if (ok != ESP_OK) {
		warn(TAG, "a_lcd_cyd() failed to create panel io");
		return ok;
	}
	info(TAG, "panel io created");
	info(TAG, "panel io: %p", io_handle);

	ok = esp_lcd_new_panel_st7789(io_handle, &a_dev_cfg, &panel);
	if (ok != ESP_OK) {
		warn(TAG, "a_lcd_cyd() failed to create panel");
		return ok;
	}
	info(TAG, "panel created");
	info(TAG, "panel: %p", panel);

	esp_lcd_panel_reset(panel);
	vTaskDelay(pdMS_TO_TICKS(50));
	try(esp_lcd_panel_init(panel));
	try(esp_lcd_panel_disp_on_off(panel, true));
	esp_lcd_panel_mirror(panel, false, false);
	esp_lcd_panel_swap_xy(panel, true);

	atomic_gpio_config_out(a_lcd_gpio_cfg.bl, false, false);
	atomic_gpio_set(a_lcd_gpio_cfg.bl, 1);

	lcd->id		   = LCD_CYD;
	lcd->panel	   = panel;
	lcd->cfg	   = &a_lcd_cfg;
	lcd->io_handle = io_handle;

	return ESP_OK;
}