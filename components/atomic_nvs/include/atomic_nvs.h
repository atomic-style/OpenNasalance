// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"

esp_err_t a_nvs_get(const char *key, nvs_type_t type, void *value);
esp_err_t a_nvs_set(const char *key, nvs_type_t type, void *value);

esp_err_t a_nvs_get_u8(const char *key, uint8_t *value);
esp_err_t a_nvs_get_i8(const char *key, int8_t *value);
esp_err_t a_nvs_get_u16(const char *key, uint16_t *value);
esp_err_t a_nvs_get_i16(const char *key, int16_t *value);
esp_err_t a_nvs_get_u32(const char *key, uint32_t *value);
esp_err_t a_nvs_get_i32(const char *key, int32_t *value);
esp_err_t a_nvs_get_u64(const char *key, uint64_t *value);
esp_err_t a_nvs_get_i64(const char *key, int64_t *value);

esp_err_t a_nvs_set_u8(const char *key, uint8_t value);
esp_err_t a_nvs_set_i8(const char *key, int8_t value);
esp_err_t a_nvs_set_u16(const char *key, uint16_t value);
esp_err_t a_nvs_set_i16(const char *key, int16_t value);
esp_err_t a_nvs_set_u32(const char *key, uint32_t value);
esp_err_t a_nvs_set_i32(const char *key, int32_t value);
esp_err_t a_nvs_set_u64(const char *key, uint64_t value);
esp_err_t a_nvs_set_i64(const char *key, int64_t value);

esp_err_t a_nvs_get_str(const char *key, char *value, size_t *value_len);
esp_err_t a_nvs_set_str(const char *key, const char *value);

esp_err_t a_nvs_erase_key(const char *key);
esp_err_t a_nvs_find_key(const char *key, nvs_type_t *type);
esp_err_t a_nvs_debug(void);
esp_err_t a_nvs_init(void);