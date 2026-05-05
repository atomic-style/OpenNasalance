#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "driver/spi_common.h"
#include "esp_intr_types.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define LCD_SPI_HOST SPI3_HOST

typedef enum {
    LCD_W5 = 0,
    LCD_CYD = 1,
    LCD_TDISPLAY = 2,
    LCD_WS_TOUCH_LCD = 3,
    LCD_RING = 7,
    LCD_ES3N28P = 8,
} a_board_id_t;

typedef enum {
    LCD_SPI = 0,
    LCD_I80 = 1,
} a_lcd_bus_t;

typedef enum {
    LCD_RGB = 0,
    LCD_BGR = 1,
} a_rgb_order_t;

typedef enum {
    LCD_RGB_BE = 0,
    LCD_RGB_LE = 1,
} a_rgb_endian_t;

typedef struct a_lcd_cfg_s {
    int h_res;
    int v_res;
    int bpp;
    a_rgb_order_t rgb;
    int max_transfer_sz;
} a_lcd_cfg_t;

typedef struct a_lcd_gpio_cfg_s {
    uint8_t cs;
    uint8_t sclk;
    uint8_t sda;
    union {
        uint8_t data0;
        uint8_t mosi;
    };
    union {
        uint8_t data1;
        uint8_t miso;
    };
    uint8_t data2;
    uint8_t data3;
    uint8_t data4;
    uint8_t data5;
    uint8_t data6;
    uint8_t data7;
    uint8_t rst;
    uint8_t dc;
    uint8_t wr;
    uint8_t rd;
    uint8_t bl;
    uint8_t te;
} a_lcd_gpio_cfg_t;

typedef struct a_lcd_s {
    esp_lcd_panel_handle_t panel;
    SemaphoreHandle_t sem;
    a_board_id_t id;
    const a_lcd_cfg_t *cfg;
    esp_lcd_panel_io_handle_t io_handle;
} a_lcd_t;