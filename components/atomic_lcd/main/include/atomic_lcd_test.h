#pragma once

#include <stdint.h>

#include "atomic_lcd_config.h"
#include "esp_err.h"

typedef struct {
    float x;
    float y;
} vec2;

typedef struct {
    int count;
    vec2 pts[8];
} poly_t;

esp_err_t atomic_lcd_test_pride(void);
esp_err_t atomic_lcd_test_blank(void);
esp_err_t atomic_lcd_test_bars(void);
esp_err_t atomic_lcd_test_red(void);
