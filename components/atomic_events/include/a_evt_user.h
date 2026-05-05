// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Event Condition Types
typedef enum {
    A_CONDITION_RED,
    A_CONDITION_YELLOW,
    A_CONDITION_GREEN,
    A_CONDITION_BLACK,
    A_CONDITION_TEST
} a_evt_condition_t;

// Event Severity Levels
typedef enum {
    A_EVENT_EMERGENCY,
    A_EVENT_ALERT,
    A_EVENT_ANNOUNCE,
    A_EVENT_DATA,
    A_EVENT_CLEAR,
    A_EVENT_DEBUG
} a_evt_severity_t;

// Event Source Types
typedef enum {
    A_EVENT_FROM_ADMIN,
    A_EVENT_FROM_USER,
    A_EVENT_FROM_SYSTEM,
    A_EVENT_FROM_PANEL,
    A_EVENT_FROM_SENSOR
} a_evt_from_t;

// Event Destination Types
typedef enum {
    A_EVENT_TO_ALL,
    A_EVENT_TO_ADMIN,
    A_EVENT_TO_USER,
    A_EVENT_TO_PANEL,
    A_EVENT_TO_SENSOR
} a_evt_to_t;

// Custom event data union for flexible payload
#define A_EVT_STRING_MAX 64
#define A_EVT_CUSTOM_DATA_MAX 32

typedef union {
    int32_t i32;
    uint32_t u32;
    int16_t i16;
    uint16_t u16;
    uint8_t u8;
    float f;
    bool b;
    char str[A_EVT_STRING_MAX];
    uint8_t bytes[A_EVT_CUSTOM_DATA_MAX];
} a_evt_custom_data_t;

// Main event structure
typedef struct {
    a_evt_condition_t condition;
    a_evt_severity_t severity;
    a_evt_from_t from;
    a_evt_to_t to;
    uint32_t event_id;  // Specific event type identifier
    a_evt_custom_data_t custom;
    uint8_t custom_size;  // Size of custom data (0 = no custom data)
} a_evt_user_data_t;

// Event handler function type
typedef void (*a_evt_user_handler_t)(const a_evt_user_data_t *event_data, void *handler_arg);

// Event base definition
ESP_EVENT_DECLARE_BASE(A_EVT_USER_EVENTS);

// Event IDs (can be extended as needed)
typedef enum {
    A_EVT_USER_ANY = -1,  // Catch-all event ID
    A_EVT_USER_MQTT_MESSAGE,
    A_EVT_USER_SENSOR_MMWAVE,
    A_EVT_USER_SENSOR_TEMPERATURE,
    A_EVT_USER_INPUT_GPIO,
    A_EVT_USER_INPUT_TOUCH,
    A_EVT_USER_SYSTEM_WIFI_LOST,
    A_EVT_USER_SYSTEM_WIFI_CONNECTED,
    A_EVT_USER_CUSTOM  // For custom event types
} a_evt_user_id_t;

/**
 * @brief Initialize the user event loop with a dedicated task
 * @return ESP_OK on success
 */
esp_err_t a_evt_user_init(void);

/**
 * @brief Post an event to the user event loop
 * @param event_id Event identifier
 * @param event_data Event data structure (will be copied)
 * @param timeout_ms Timeout in milliseconds (portMAX_DELAY for infinite)
 * @return ESP_OK on success
 */
esp_err_t a_evt_user_post(a_evt_user_id_t event_id, const a_evt_user_data_t *event_data, int timeout_ms);

/**
 * @brief Register a handler for specific event IDs
 * @param event_id Event ID to listen for (A_EVT_USER_ANY for all events)
 * @param handler Handler function
 * @param handler_arg Optional argument passed to handler
 * @return ESP_OK on success
 */
esp_err_t a_evt_user_register_handler(a_evt_user_id_t event_id, a_evt_user_handler_t handler, void *handler_arg);

/**
 * @brief Get the user event loop handle
 * @return Event loop handle or NULL if not initialized
 */
esp_event_loop_handle_t a_evt_user_get_loop(void);

#ifdef __cplusplus
}
#endif