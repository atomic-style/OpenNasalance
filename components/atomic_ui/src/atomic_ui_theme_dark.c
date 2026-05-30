// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Default dark theme. Background is the OpenNasalance dark blue (0x21233a)
// from the existing splash; accents tuned for clinical readability.

#include "atomic_ui_theme.h"

LV_FONT_DECLARE(prompt);
LV_FONT_DECLARE(prompt_semibold);

extern const uint8_t splash_jpg_start[] asm("_binary_splash_jpg_start");
extern const uint8_t splash_jpg_end[] asm("_binary_splash_jpg_end");

const a_ui_theme_t a_ui_theme_dark = {
    .name = "dark",

    .bg = LV_COLOR_MAKE(0x21, 0x23, 0x3a),
    .fg = LV_COLOR_MAKE(0xea, 0xea, 0xea),
    .accent = LV_COLOR_MAKE(0x00, 0xb3, 0xff),
    .warn = LV_COLOR_MAKE(0xff, 0xc0, 0x40),
    .danger = LV_COLOR_MAKE(0xff, 0x33, 0x44),

    .panel_bg = LV_COLOR_MAKE(0x18, 0x1a, 0x2c),
    .panel_border = LV_COLOR_MAKE(0x3a, 0x3d, 0x55),

    .corner_rad = 4,
    .panel_pad = 6,
    .border_width = 1,
    .indicator_diameter = 10,

    .role_font =
        {
            [A_UI_ROLE_BODY] = &prompt,
            [A_UI_ROLE_HEADER] = &prompt_semibold,
            [A_UI_ROLE_STATUS] = &prompt,
            [A_UI_ROLE_VALUE] = &prompt,
            [A_UI_ROLE_CAPTION] = &prompt,
            [A_UI_ROLE_TITLE] = &prompt_semibold,
        },
    .role_color =
        {
            [A_UI_ROLE_BODY] = LV_COLOR_MAKE(0xea, 0xea, 0xea),
            [A_UI_ROLE_HEADER] = LV_COLOR_MAKE(0xff, 0xff, 0xff),
            [A_UI_ROLE_STATUS] = LV_COLOR_MAKE(0xa0, 0xa6, 0xc0),
            [A_UI_ROLE_VALUE] = LV_COLOR_MAKE(0x00, 0xb3, 0xff),
            [A_UI_ROLE_CAPTION] = LV_COLOR_MAKE(0x80, 0x86, 0x9c),
            [A_UI_ROLE_TITLE] = LV_COLOR_MAKE(0xff, 0xff, 0xff),
        },

    .indicator_color =
        {
            [A_UI_IND_OFF] = LV_COLOR_MAKE(0x40, 0x44, 0x5a),
            [A_UI_IND_ON] = LV_COLOR_MAKE(0x55, 0xd6, 0x82),
            [A_UI_IND_WARN] = LV_COLOR_MAKE(0xff, 0xc0, 0x40),
            [A_UI_IND_DANGER] = LV_COLOR_MAKE(0xff, 0x33, 0x44),
        },

    .splash_start = splash_jpg_start,
    .splash_end = splash_jpg_end,
};
