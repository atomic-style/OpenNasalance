#include <stdint.h>
#include "atomic_lcd_rgb.h"

static const char *TAG = "〛";

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) { // 0-255
    return (((r / 8) << 11) | ((g / 4) << 5) | (b / 8));
}

uint16_t rgb565_perc(uint8_t r, uint8_t g, uint8_t b) { // 0-100
    uint32_t rs = (r * 255 / 100);
    uint32_t gs = (g * 255 / 100);
    uint32_t bs = (b * 255 / 100);
    return rgb565((uint8_t)rs, (uint8_t)gs, (uint8_t)bs);
}

uint16_t rgb565_scale(uint32_t r, uint32_t g, uint32_t b, uint32_t scale) { // 0-scale
    uint32_t rs = (r * 255 / scale);
    uint32_t gs = (g * 255 / scale);
    uint32_t bs = (b * 255 / scale);
    return rgb565((uint8_t)rs, (uint8_t)gs, (uint8_t)bs);
}

uint16_t reverse_endian_16(uint16_t number) {
  return (number << 8) | (number >> 8);
}

atomic_rgb_t atomic_rgb_from_uint16(uint16_t c) {
    uint8_t r = (c >> 11) & 0x1F;
    uint8_t g = (c >> 5) & 0x3F;
    uint8_t b = c & 0x1F;
    return (atomic_rgb_t){
        .r = (uint8_t)r << 3,
        .g = (uint8_t)g << 2,
        .b = (uint8_t)b << 3,
    };
}