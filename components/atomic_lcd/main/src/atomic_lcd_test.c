
#include "atomic_lcd_test.h"

#include "atomic_err.h"
#include "atomic_lcd.h"
#include "atomic_lcd_rgb.h"
#include "atomic_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "LCD Test";

static a_lcd_t *s_lcd;
static uint32_t s_h_res;
static uint32_t s_v_res;
static uint32_t s_pixels;
static uint32_t s_chunks = 8;
static uint32_t s_lines_per_chunk;
static uint32_t s_lines_remainder;
static uint32_t s_buffer_size_pixels;
static uint32_t s_buffer_size_bytes;
static bool s_rotate = false;
static uint16_t *s_buffer;
static uint16_t *s_rotation_buffer;

static esp_err_t atomic_lcd_test_config(void) {
    // temporarily using arbitrary buffer size of resolution / 8
    if (!s_lcd) {
        s_lcd = a_lcd_get();
        if (s_lcd->id == LCD_W5) {
            s_rotate = true;
            s_h_res = s_lcd->cfg->v_res;
            s_v_res = s_lcd->cfg->h_res;
        } else {
            s_rotate = false;
            s_h_res = s_lcd->cfg->h_res;
            s_v_res = s_lcd->cfg->v_res;
        }
        s_chunks = 8;
        s_pixels = s_h_res * s_v_res;
        s_buffer_size_pixels = s_pixels / s_chunks;
        s_buffer_size_bytes = s_buffer_size_pixels * sizeof(uint16_t);
        s_lines_per_chunk = s_buffer_size_pixels / s_h_res;
        s_lines_remainder = (s_pixels % s_chunks) / s_h_res;
        if (s_lines_per_chunk * s_chunks < s_v_res) {
            s_chunks++;
        }
    }
    return ESP_OK;
}

static esp_err_t atomic_lcd_test_buffer_alloc(void) {
    uint32_t caps = MALLOC_CAP_8BIT | MALLOC_CAP_DMA;
    s_buffer = (uint16_t *)heap_caps_malloc(s_buffer_size_bytes, caps);
    if (s_buffer) {
        info(TAG, "DMA buffer allocated, size: %d", s_buffer_size_bytes);
        memset(s_buffer, 0, s_buffer_size_bytes);
        return ESP_OK;
    } else {
        s_buffer = (uint16_t *)heap_caps_malloc(s_buffer_size_bytes, MALLOC_CAP_8BIT);
        if (s_buffer) {
            info(TAG, "8BIT buffer allocated, size: %d", s_buffer_size_bytes);
        } else {
            err(TAG, "Failed to allocate main buffer");
            return ESP_ERR_NO_MEM;
        }
    }
    memset(s_buffer, 0, s_buffer_size_bytes);
    return ESP_OK;
}

static esp_err_t atomic_lcd_test_buffer_rotation_alloc(void) {
    uint32_t caps = MALLOC_CAP_8BIT | MALLOC_CAP_DMA;
    s_rotation_buffer = (uint16_t *)heap_caps_malloc(s_buffer_size_bytes, caps);
    if (s_rotation_buffer) {
        info(TAG, "DMA rotation buffer allocated, size: %d", s_buffer_size_bytes);
        memset(s_rotation_buffer, 0, s_buffer_size_bytes);
        return ESP_OK;
    } else {
        s_rotation_buffer = (uint16_t *)heap_caps_malloc(s_buffer_size_bytes, MALLOC_CAP_8BIT);
        if (s_rotation_buffer) {
            info(TAG, "8BIT rotation buffer allocated, size: %d", s_buffer_size_bytes);
        } else {
            err(TAG, "Failed to allocate rotation buffer");
            return ESP_ERR_NO_MEM;
        }
    }
    memset(s_rotation_buffer, 0, s_buffer_size_bytes);
    return ESP_OK;
}

static esp_err_t atomic_lcd_test_buffer_free(void) {
    if (s_buffer) {
        info(TAG, "freeing buffer");
        heap_caps_free(s_buffer);
        s_buffer = NULL;
    }
    if (s_rotation_buffer) {
        info(TAG, "freeing rotation buffer");
        heap_caps_free(s_rotation_buffer);
        s_rotation_buffer = NULL;
    }
    info(TAG, "buffers freed");
    return ESP_OK;
}

static esp_err_t atomic_lcd_test_init(void) {
    try(atomic_lcd_test_config());
    try(atomic_lcd_test_buffer_alloc());
    if (s_rotate) {
        try(atomic_lcd_test_buffer_rotation_alloc());
    }
    return ESP_OK;
}

static uint16_t a_lcd_col(uint8_t r, uint8_t g, uint8_t b) {
    return reverse_endian_16(rgb565((uint8_t)r, (uint8_t)g, (uint8_t)b));
}

static esp_err_t a_lcd_buffer_fill_color(size_t pixels, uint16_t color) {
    for (size_t i = 0; i < pixels; i++) {
        s_buffer[i] = color;
    }
    return ESP_OK;
}

static esp_err_t a_lcd_buffer_fill_rgb(size_t pixels, uint8_t r, uint8_t g, uint8_t b) {
    for (size_t i = 0; i < pixels; i++) {
        s_buffer[i] = a_lcd_col(r, g, b);
    }
    return ESP_OK;
}

static esp_err_t draw_col_rotate(uint16_t color, uint32_t x1, uint32_t x2, uint32_t y1, uint32_t y2) {
    const int logical_width = x2 - x1 + 1;
    const int logical_height = y2 - y1 + 1;
    const int rotated_width = logical_height;
    const int rotated_height = logical_width;
    const int logical_height_total = s_v_res;

    a_lcd_buffer_fill_color(logical_width * logical_height, reverse_endian_16(color));

    const int min_px = logical_height_total - 1 - y2;
    const int min_py = x1;

    uint16_t *src_pixels = s_buffer;
    uint16_t *dst_pixels = s_rotation_buffer;

    for (int ly = y1; ly <= y2; ly++) {
        int local_y = ly - y1;
        int src_row_start = local_y * logical_width;

        for (int lx = x1; lx <= x2; lx++) {
            int local_x = lx - x1;
            int src_index = src_row_start + local_x;
            uint16_t src_color = src_pixels[src_index];

            int px = logical_height_total - 1 - ly;
            int py = lx;
            int dx = px - min_px;
            int dy = py - min_py;
            int dst_index = dy * rotated_width + dx;

            if (dst_index >= 0 && dst_index < (rotated_width * rotated_height)) {
                dst_pixels[dst_index] = src_color;
            }
        }
    }

    int panel_x_start = min_px;
    int panel_y_start = min_py;
    int panel_x_end = panel_x_start + rotated_width;
    int panel_y_end = panel_y_start + rotated_height;

    try(esp_lcd_panel_draw_bitmap(s_lcd->panel, panel_x_start, panel_y_start, panel_x_end, panel_y_end, dst_pixels));
    xSemaphoreTake(s_lcd->sem, portMAX_DELAY);
    return ESP_OK;
}

static esp_err_t draw_col(uint16_t color, uint32_t x1, uint32_t x2, uint32_t y1, uint32_t y2) {
    if (s_rotate) {
        return draw_col_rotate(color, x1, x2, y1, y2);
        return ESP_OK;
    } else {
        uint32_t size = (x2 - x1 + 1) * (y2 - y1 + 1);
        a_lcd_buffer_fill_color(size, reverse_endian_16(color));
        esp_err_t ok = esp_lcd_panel_draw_bitmap(s_lcd->panel, x1, y1, x2 + 1, y2 + 1, s_buffer);
        if (ok != ESP_OK) {
            err(TAG, "failed to draw bitmap");
            return ESP_FAIL;
        }
        xSemaphoreTake(s_lcd->sem, portMAX_DELAY);
        return ESP_OK;
    }
}

esp_err_t atomic_lcd_test_bars(void) {
    debug(TAG, "atomic_lcd_test_bars()");
    try(atomic_lcd_test_blank());
    try(atomic_lcd_test_init());

    uint16_t s_width = s_h_res / 9;
    uint16_t g_width_l = (s_h_res - (s_width * 7)) / 2;
    uint16_t g_width_r = s_h_res - (s_width * 7 + g_width_l);

    uint16_t gray = rgb565(102, 102, 102);
    uint8_t col[7][3] = {
        {255, 255, 255}, // white
        {255, 255, 0},   // yellow
        {0, 255, 255},   // cyan
        {0, 255, 0},     // green
        {255, 0, 255},   // pink
        {255, 0, 0},     // red
        {0, 0, 255},     // blue
    };

    // gray
    try(draw_col(gray, 0, g_width_l - 1, 0, s_v_res - 1));
    try(draw_col(gray, s_h_res - g_width_r, s_h_res - 1, 0, s_v_res - 1));

    // full intensity
    uint32_t y1 = 0;
    uint32_t y2 = s_v_res * 0.12; // 12%

    for (uint32_t i = 0; i < 7; i++) {
        try(draw_col(rgb565(col[i][0], col[i][1], col[i][2]), (i * s_width) + g_width_l,
                     ((i + 1) * s_width) + g_width_l - 1, y1, y2 - 1));
    }

    // 75% intensity
    y1 = y2;
    y2 = s_v_res * 0.59;
    for (uint32_t i = 0; i < 7; i++) {
        uint16_t color = rgb565(col[i][0] * 0.75, col[i][1] * 0.75, col[i][2] * 0.75);
        try(draw_col(color, (i * s_width) + g_width_l, ((i + 1) * s_width) + g_width_l - 1, y1, y2 - 1));
    }

    // grayscale white
    y1 = y2;
    y2 = y1 + (s_v_res * 0.12);
    try(draw_col(rgb565(col[0][0] * 0.75, col[0][1] * 0.75, col[0][2] * 0.75), 0, g_width_l - 1, y1, y2 - 1));
    try(draw_col(rgb565(col[0][0] * 0.75, col[0][1] * 0.75, col[0][2] * 0.75), s_h_res - g_width_r, s_h_res - 1, y1,
                 y2 - 1));

    // grayscale boxes
    for (uint32_t i = 0; i < 14; i++) {
        uint32_t val = (255 / 13) * i;
        uint16_t gray = rgb565(val, val, val);
        uint32_t x1 = ((i / 2) * s_width) + g_width_l;
        uint32_t x2 = (((i / 2) + 1) * s_width) + g_width_l;
        if (i % 2 == 1) {
            x1 += s_width / 2;
        }
        try(draw_col(gray, x1, x2 - 1, y1, y2 - 1));
    }

    // ramp
    y1 = y2;
    y2 = y1 + (s_v_res * 0.12);
    // put one line in buffer
    for (uint32_t i = 0; i < s_h_res; i++) {
        double step = (double)255 / (double)s_h_res;
        uint32_t val = (uint32_t)(step * i); // h_res - (step * i);
        uint16_t color = rgb565(val, val, val);
        s_buffer[i] = reverse_endian_16(color);
    }
    uint32_t ramp_lines = y2 - y1;
    for (int l = 0; l < ramp_lines; l++) {
        if (s_rotate) {
            try(esp_lcd_panel_draw_bitmap(s_lcd->panel, y1 + l, 0, y1 + l + 1, s_h_res, s_buffer));
        } else {
            try(esp_lcd_panel_draw_bitmap(s_lcd->panel, 0, y1 + l, s_h_res, y1 + l + 1, s_buffer));
        }
        xSemaphoreTake(s_lcd->sem, portMAX_DELAY);
    }

    // 709 part 1
    y1 = y2;
    y2 = s_v_res - 1;
    uint32_t w1 = g_width_l / 3;
    uint16_t c709 = rgb565(179, 179, 79);
    try(draw_col(c709, 0, w1, y1, y2));
    c709 = rgb565(134, 177, 179);
    try(draw_col(c709, w1 + 1, w1 + w1, y1, y2));
    c709 = rgb565(128, 176, 74);
    try(draw_col(c709, w1 + w1 + 1, g_width_l - 1, y1, y2));

    // blacks
    uint32_t bx1 = g_width_l;
    uint32_t bx2 = s_h_res / 2;
    double bw = (double)(bx2 - bx1) / 10;
    uint8_t black[10] = {1, 1, 0, 0, 1, 0, 3, 1, 1, 1};
    for (uint32_t i = 0; i < 10; i++) {
        uint16_t color = rgb565((black[i] * 255) / 10, (black[i] * 255) / 10, (black[i] * 255) / 10);
        uint32_t x1 = bx1 + (i * bw);
        uint32_t x2 = bx1 + ((i + 1) * bw) - 1;
        try(draw_col(color, x1, x2, y1, y2));
    }

    // whites
    uint32_t wx1 = s_h_res / 2;
    uint32_t wx2 = wx1 + (bw * 6) - 1;
    try(draw_col(rgb565(255, 255, 255), wx1, wx2, y1, y2));
    try(draw_col(rgb565(0, 0, 0), wx2 + 1, (s_h_res - g_width_r) - 1, y1, y2));

    // 709 part 2
    wx1 = s_h_res - g_width_r;
    w1 = g_width_r / 3;
    c709 = rgb565(162, 71, 176);
    try(draw_col(c709, wx1, wx1 + w1, y1, y2));
    c709 = rgb565(159, 67, 41);
    try(draw_col(c709, wx1 + w1 + 1, wx1 + w1 + w1, y1, y2));
    c709 = rgb565(56, 36, 175);
    try(draw_col(c709, wx1 + w1 + w1 + 1, s_h_res - 1, y1, y2));

    try(atomic_lcd_test_buffer_free());
    return ESP_OK;
}

esp_err_t atomic_lcd_test_pride(void) {
    try(atomic_lcd_test_init());
    info(TAG, "atomic_lcd_test_pride() h_res=%d, v_res=%d", s_h_res, s_v_res);
    uint32_t stripe_height = s_v_res / 6;
    uint32_t top = 0;
    uint32_t remainder = s_v_res - (stripe_height * 6);

    if (remainder > 0) {
        top = remainder / 2;
        a_lcd_buffer_fill_rgb(s_h_res * (top + 1), 0, 0, 0);
        esp_lcd_panel_draw_bitmap(s_lcd->panel, 0, 0, s_h_res, top + 1, s_buffer);
        xSemaphoreTake(s_lcd->sem, portMAX_DELAY);
        esp_lcd_panel_draw_bitmap(s_lcd->panel, 0, s_v_res - top, s_h_res, s_v_res, s_buffer);
        xSemaphoreTake(s_lcd->sem, portMAX_DELAY);
    }

    uint16_t p[6] = {0xf800, 0xfc60, 0xff60, 0x0403, 0x027f, 0x7811};
    for (uint8_t i = 0; i < 6; i++) {
        a_lcd_buffer_fill_color(s_buffer_size_pixels, reverse_endian_16(p[i]));
        uint32_t y1 = (i * stripe_height) + top;
        uint32_t y2 = y1 + (stripe_height / 2) + 1;

        debug(TAG, "drawing col %d-1, %d, %d, %d, %d", i, 0, y1, s_h_res, y2 + 1);
        esp_lcd_panel_draw_bitmap(s_lcd->panel, 0, y1, s_h_res, y2 + 1, s_buffer);
        xSemaphoreTake(s_lcd->sem, portMAX_DELAY);
        y1 = y2;
        y2 = y1 + (stripe_height / 2) + 1;
        debug(TAG, "drawing col %d-2, %d, %d, %d, %d", i, 0, y1, s_h_res, y2 + 1);
        esp_lcd_panel_draw_bitmap(s_lcd->panel, 0, y1, s_h_res, y2 + 1, s_buffer);
        xSemaphoreTake(s_lcd->sem, portMAX_DELAY);
    }

    try(atomic_lcd_test_buffer_free());
    return ESP_OK;
}

esp_err_t atomic_lcd_test_red(void) {
    info(TAG, "atomic_lcd_test_red()");
    try(atomic_lcd_test_init());
    for (int i = 0; i < s_h_res; i++) {
        s_buffer[i] = a_lcd_col(i * 255 / s_h_res, 0, 0);
    }
    for (uint32_t y = 0; y < s_v_res; y++) {
        esp_lcd_panel_draw_bitmap(s_lcd->panel, 0, y, s_h_res, y + 1, s_buffer);
        xSemaphoreTake(s_lcd->sem, portMAX_DELAY);
    }
    try(atomic_lcd_test_buffer_free());
    return ESP_OK;
}

esp_err_t atomic_lcd_test_blank(void) {
    info(TAG, "atomic_lcd_test_blank()");
    try(atomic_lcd_test_init());
    a_lcd_buffer_fill_rgb(s_buffer_size_pixels, 0, 0, 0);
    uint32_t y1, y2 = 0;
    for (uint32_t c = 0; c < s_chunks; c++) {
        y1 = c * s_lines_per_chunk;
        y2 = (y1 + s_lines_per_chunk > s_v_res) ? s_v_res : y1 + s_lines_per_chunk;
        esp_lcd_panel_draw_bitmap(s_lcd->panel, 0, y1, s_h_res, y2 + 1, s_buffer);
        xSemaphoreTake(s_lcd->sem, portMAX_DELAY);
    }
    try(atomic_lcd_test_buffer_free());
    return ESP_OK;
}
