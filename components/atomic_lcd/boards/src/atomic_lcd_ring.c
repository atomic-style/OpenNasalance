#include "atomic_lcd_ring.h"
#include "atomic_err.h"
#include "atomic_gpio.h"
#include "atomic_lcd_axs.h"
#include "atomic_lcd_config.h"
#include "atomic_log.h"
#include "driver/spi_common.h"
#include "esp_intr_types.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_ops.h"

static const char *TAG = "RING";

#define RING_H_RES 472
#define RING_V_RES 466
#define RING_BPP 16
#define RING_RGB_ELE_ORDER LCD_RGB
#define RING_MAX_TRANSFER_SZ 7680

static const a_lcd_cfg_t a_lcd_cfg = {
    .h_res = RING_H_RES,
    .v_res = RING_V_RES,
    .bpp = RING_BPP,
    .rgb = RING_RGB_ELE_ORDER,
    .max_transfer_sz = RING_MAX_TRANSFER_SZ,
};

static const a_lcd_gpio_cfg_t a_lcd_gpio_cfg = {
    .cs = 7,
    .sclk = 13,
    .data0 = 12,
    .data1 = 8,
    .data2 = 14,
    .data3 = 9,
    .data4 = -1,
    .data5 = -1,
    .data6 = -1,
    .data7 = -1,
    .rst = 11,
    .dc = -1,
    .bl = 40,
    .te = 10,
};

static esp_lcd_panel_io_spi_config_t a_spi_io_cfg = {
    .cs_gpio_num = 7,
    .dc_gpio_num = -1,
    .spi_mode = 0,
    .pclk_hz = 40 * 1000 * 1000,
    .trans_queue_depth = 10,
    .on_color_trans_done = NULL,
    .user_ctx = NULL,
    .lcd_cmd_bits = 32,
    .lcd_param_bits = 8,
    .flags =
        {
            .quad_mode = true,
        },
};

static const spi_bus_config_t a_spi_bus_cfg = {
    .sclk_io_num = 13,
    .data0_io_num = 12,
    .data1_io_num = 8,
    .data2_io_num = 14,
    .data3_io_num = 9,
    .data4_io_num = -1,
    .data5_io_num = -1,
    .data6_io_num = -1,
    .data7_io_num = -1,
    .max_transfer_sz = (uint16_t)RING_MAX_TRANSFER_SZ,
};

static co5300_lcd_init_cmd_t init_cmds[] = {
    {0xFE, (uint8_t[]){0x00}, 0, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 0, 10},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 10},
    {0x63, (uint8_t[]){0xFF}, 1, 10},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xDD}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 0},
    {0x11, (uint8_t[]){0x00}, 0, 60},
    {0x29, (uint8_t[]){0x00}, 0, 0},
};

static co5300_vendor_config_t a_vendor_cfg = {
    .init_cmds = init_cmds,
    .init_cmds_size = (sizeof(init_cmds) / sizeof(init_cmds[0])),
    .flags = {.use_qspi_interface = 1},
};

static esp_lcd_panel_dev_config_t a_dev_cfg = {
    .reset_gpio_num = 11,
    .rgb_ele_order = RING_RGB_ELE_ORDER,
    .bits_per_pixel = 16,
    .vendor_config = (void *)&a_vendor_cfg,
};

esp_err_t a_lcd_ring(a_lcd_t *lcd, bool (*cb)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *)) {
    info(TAG, "a_lcd_ring()");
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io_handle = NULL;

    info(TAG, "initializing qspi bus host: %d, channel: %d", LCD_SPI_HOST, SPI_DMA_CH_AUTO);
    try(spi_bus_initialize(LCD_SPI_HOST, &a_spi_bus_cfg, SPI_DMA_CH_AUTO));
    a_spi_io_cfg.on_color_trans_done = cb;
    try(esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &a_spi_io_cfg, &io_handle));

    try(esp_lcd_new_panel_co5300(io_handle, &a_dev_cfg, &panel));
    try(esp_lcd_panel_reset(panel));
    vTaskDelay(pdMS_TO_TICKS(200));
    try(esp_lcd_panel_init(panel));
    try(esp_lcd_panel_disp_on_off(panel, true));

    atomic_gpio_config_out(a_lcd_gpio_cfg.bl, false, false);
    atomic_gpio_set(a_lcd_gpio_cfg.bl, 1);

    lcd->id = LCD_RING;
    lcd->panel = panel;
    lcd->cfg = &a_lcd_cfg;
    lcd->io_handle = io_handle;

    info(TAG, "a_lcd_ring() display done.");
    return ESP_OK;
}