// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} atomic_rgb_t;

/**
 * @brief Create RGB565 color from uint8_t RGB values
 *
 * @note  RGB values are 0-255
 *
 * @param[in] r uint8_t red value 0-255
 * @param[in] g uint8_t green value 0-255
 * @param[in] b uint8_t blue value 0-255
 * @param[out] rgb565 Returned RGB565 color
 * @return uint16_t RGB565 color
 */
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Create RGB565 color from 0-100 RGB values
 *
 * @note  RGB values are 0-100
 *
 * @param[in] r uint8_t red value 0-100
 * @param[in] g uint8_t green value 0-100
 * @param[in] b uint8_t blue value 0-100
 * @param[out] rgb565 Returned RGB565 color
 * @return uint16_t RGB565 color
*/
uint16_t rgb565_perc(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Create RGB565 color from uint32_t RGB values
 *
 * @note  Maximum value is scale
 *
 * @param[in] r uint32_t red value
 * @param[in] g uint32_t green value
 * @param[in] b uint32_t blue value
 * @param[in] scale uint32_t maximum value
 * @param[out] rgb565 Returned RGB565 color
 * @return uint16_t RGB565 color
 */
uint16_t rgb565_scale(uint32_t r, uint32_t g, uint32_t b, uint32_t scale);

uint16_t reverse_endian_16(uint16_t number);
atomic_rgb_t atomic_rgb_from_uint16(uint16_t c);

#ifdef __cplusplus
}
#endif