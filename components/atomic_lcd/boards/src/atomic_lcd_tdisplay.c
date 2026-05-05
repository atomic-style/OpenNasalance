#include "atomic_lcd_tdisplay.h"

#include "atomic_err.h"
#include "atomic_gpio.h"
#include "atomic_lcd_config.h"
#include "atomic_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"

#define TDISPLAY_H_RES 320
#define TDISPLAY_V_RES 170
#define TDISPLAY_BPP 16
#define TDISPLAY_RGB_ELE_ORDER LCD_RGB
#define TDISPLAY_MAX_TRANSFER_SZ (TDISPLAY_H_RES * 64 * TDISPLAY_BPP / 8)

static const char *TAG = "䋧";

static const a_lcd_cfg_t a_lcd_cfg = {
    .h_res = TDISPLAY_H_RES,
    .v_res = TDISPLAY_V_RES,
    .bpp = TDISPLAY_BPP,
    .rgb = TDISPLAY_RGB_ELE_ORDER,
    .max_transfer_sz = TDISPLAY_MAX_TRANSFER_SZ,
};

static const a_lcd_gpio_cfg_t a_lcd_gpio_cfg = {
    .cs = 6,
    .sclk = 17,
    .sda = 18,
    .data0 = 39,
    .data1 = 40,
    .data2 = 41,
    .data3 = 42,
    .data4 = 45,
    .data5 = 46,
    .data6 = 47,
    .data7 = 48,
    .rst = 5,
    .dc = 7,
    .wr = 8,
    .rd = 9,
    .bl = 38,
    .te = -1,
};

static lcd_cmd_t lcd_st7789v[] = {
    {0x11, {0}, 0 | 0x80},
    {0x3A, {0X05}, 1},
    {0xB2, {0X0B, 0X0B, 0X00, 0X33, 0X33}, 5},
    {0xB7, {0X75}, 1},
    {0xBB, {0X28}, 1},
    {0xC0, {0X2C}, 1},
    {0xC2, {0X01}, 1},
    {0xC3, {0X1F}, 1},
    {0xC6, {0X13}, 1},
    {0xD0, {0XA7}, 1},
    {0xD0, {0XA4, 0XA1}, 2},
    {0xD6, {0XA1}, 1},
    {0xE0, {0XF0, 0X05, 0X0A, 0X06, 0X06, 0X03, 0X2B, 0X32, 0X43, 0X36, 0X11, 0X10, 0X2B, 0X32}, 14},
    {0xE1, {0XF0, 0X08, 0X0C, 0X0B, 0X09, 0X24, 0X2B, 0X22, 0X43, 0X38, 0X15, 0X16, 0X2F, 0X37}, 14},
};

static esp_lcd_i80_bus_config_t a_i80_bus_cfg = {.dc_gpio_num = 7,
                                                 .wr_gpio_num = 8,
                                                 .clk_src = LCD_CLK_SRC_DEFAULT,
                                                 .data_gpio_nums = {39, 40, 41, 42, 45, 46, 47, 48},
                                                 .bus_width = 8,
                                                 .max_transfer_bytes = TDISPLAY_MAX_TRANSFER_SZ};

static esp_lcd_panel_io_i80_config_t a_i80_io_cfg = {
    .cs_gpio_num = 6,
    .pclk_hz = 10 * 1000 * 1000,
    .trans_queue_depth = 10,
    .lcd_cmd_bits = 8,
    .lcd_param_bits = 8,
    .dc_levels =
        {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
    .on_color_trans_done = NULL,
};

static esp_lcd_panel_dev_config_t a_i80_panel_config = {
    .reset_gpio_num = 5, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 16, .vendor_config = NULL};

esp_err_t a_lcd_tdisplay(a_lcd_t *lcd, bool (*cb)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *)) {
    // info(TAG, "a_lcd_tdisplay(lcd: %p)", lcd);
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_i80_bus_handle_t i80_bus = NULL;
    esp_lcd_panel_io_handle_t io_handle = NULL;

    a_i80_io_cfg.on_color_trans_done = cb;

    atomic_gpio_config_out(a_lcd_gpio_cfg.rd, false, false);
    atomic_gpio_set(a_lcd_gpio_cfg.rd, 1);

    try(esp_lcd_new_i80_bus(&a_i80_bus_cfg, &i80_bus));
    try(esp_lcd_new_panel_io_i80(i80_bus, &a_i80_io_cfg, &io_handle));
    try(esp_lcd_new_panel_st7789(io_handle, &a_i80_panel_config, &panel));

    try(esp_lcd_panel_reset(panel));
    try(esp_lcd_panel_init(panel));
    try(esp_lcd_panel_invert_color(panel, true));
    try(esp_lcd_panel_swap_xy(panel, true));
    try(esp_lcd_panel_mirror(panel, false, true));
    try(esp_lcd_panel_set_gap(panel, 0, 35));

    for (uint8_t i = 0; i < (sizeof(lcd_st7789v) / sizeof(lcd_cmd_t)); i++) {
        try(esp_lcd_panel_io_tx_param(io_handle, lcd_st7789v[i].addr, lcd_st7789v[i].param, lcd_st7789v[i].len & 0x7f));
        if (lcd_st7789v[i].len & 0x80)
            vTaskDelay(pdMS_TO_TICKS(120));
    }

    try(esp_lcd_panel_disp_on_off(panel, true));

    atomic_gpio_config_out(a_lcd_gpio_cfg.bl, false, false);
    atomic_gpio_set(a_lcd_gpio_cfg.bl, 1);

    lcd->id = LCD_TDISPLAY;
    lcd->panel = panel;
    lcd->cfg = &a_lcd_cfg;
    lcd->io_handle = io_handle;

    // info(TAG, "a_lcd_tdisplay done.");

    return ESP_OK;
}