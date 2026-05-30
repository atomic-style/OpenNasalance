// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "atomic_ui_theme.h"

static const a_ui_theme_t *s_active = &a_ui_theme_dark;

void a_ui_theme_set(const a_ui_theme_t *theme) {
    s_active = theme ? theme : &a_ui_theme_dark;
}

const a_ui_theme_t *a_ui_theme_get(void) {
    return s_active;
}

const lv_font_t *a_ui_theme_role_font(a_ui_role_t role) {
    if (role < 0 || role >= A_UI_ROLE_COUNT) role = A_UI_ROLE_BODY;
    const lv_font_t *f = s_active->role_font[role];
    return f ? f : s_active->role_font[A_UI_ROLE_BODY];
}

lv_color_t a_ui_theme_role_color(a_ui_role_t role) {
    if (role < 0 || role >= A_UI_ROLE_COUNT) role = A_UI_ROLE_BODY;
    return s_active->role_color[role];
}

lv_color_t a_ui_theme_indicator_color(a_ui_indicator_state_t state) {
    if (state < 0 || state >= A_UI_IND_COUNT) state = A_UI_IND_OFF;
    return s_active->indicator_color[state];
}
