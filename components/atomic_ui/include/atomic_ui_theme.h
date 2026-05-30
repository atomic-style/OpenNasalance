// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Theme + role contract for atomic_ui.
//
// Call sites name what a thing IS (a role); the active theme decides how it
// looks. Roles and indicator states are array-indexed in a_ui_theme_t so
// adding a new one only touches the enum and any theme tables.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    A_UI_ROLE_BODY = 0,
    A_UI_ROLE_HEADER,
    A_UI_ROLE_STATUS,
    A_UI_ROLE_VALUE,
    A_UI_ROLE_CAPTION,
    A_UI_ROLE_TITLE,
    A_UI_ROLE_COUNT,
} a_ui_role_t;

typedef enum {
    A_UI_IND_OFF = 0,
    A_UI_IND_ON,
    A_UI_IND_WARN,
    A_UI_IND_DANGER,
    A_UI_IND_COUNT,
} a_ui_indicator_state_t;

typedef struct {
    const char *name;

    lv_color_t bg;
    lv_color_t fg;
    lv_color_t accent;
    lv_color_t warn;
    lv_color_t danger;

    lv_color_t panel_bg;
    lv_color_t panel_border;

    int32_t corner_rad;
    int32_t panel_pad;
    int32_t border_width;
    int32_t indicator_diameter;

    const lv_font_t *role_font[A_UI_ROLE_COUNT];
    lv_color_t       role_color[A_UI_ROLE_COUNT];

    lv_color_t indicator_color[A_UI_IND_COUNT];

    // Optional embedded splash bytes (e.g. JPEG). NULL → no splash.
    const uint8_t *splash_start;
    const uint8_t *splash_end;
} a_ui_theme_t;

extern const a_ui_theme_t a_ui_theme_dark;

void                a_ui_theme_set(const a_ui_theme_t *theme);
const a_ui_theme_t *a_ui_theme_get(void);

const lv_font_t *a_ui_theme_role_font(a_ui_role_t role);
lv_color_t       a_ui_theme_role_color(a_ui_role_t role);
lv_color_t       a_ui_theme_indicator_color(a_ui_indicator_state_t state);

#ifdef __cplusplus
}
#endif
