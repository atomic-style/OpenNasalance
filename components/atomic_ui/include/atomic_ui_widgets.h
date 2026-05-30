// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase-1 widget factories for atomic_ui. Each factory takes a config struct
// (use designated initializers) and returns a styled lv_obj_t * the caller
// can hand straight to LVGL APIs.
//
//   Sizing convention: cfg.w == 0 means "fill parent" (LV_PCT(100)). Pass
//   LV_SIZE_CONTENT explicitly for shrink-to-fit.

#pragma once

#include "atomic_ui_theme.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Bundled fonts ----------------------------------------------------------
// Prompt (regular + semibold) shipped with atomic_ui. Pass &prompt / &
// prompt_semibold anywhere LVGL wants a const lv_font_t *.
extern const lv_font_t prompt;
extern const lv_font_t prompt_18;
extern const lv_font_t prompt_48;
extern const lv_font_t prompt_semibold;
extern const lv_font_t prompt_semibold_18;
extern const lv_font_t prompt_semibold_48;

// --- Simple primitives ------------------------------------------------------
// Plain text label with explicit font + color. Use this when you want direct
// control; for role/theme-driven labels use a_ui_label_create() instead.
typedef struct {
    lv_obj_t *parent; // NULL → active screen
    const char *text;
    int32_t x, y;
    const lv_font_t *font; // NULL → theme body font
    lv_color_t color;
} a_ui_text_cfg_t;

lv_obj_t *a_ui_text_create(const a_ui_text_cfg_t *cfg);

// Filled rectangle (no border, no radius). Useful as a coloured bar/divider.
typedef struct {
    lv_obj_t *parent; // NULL → active screen
    int32_t x, y;
    int32_t w, h;
    lv_color_t color;
} a_ui_rect_cfg_t;

lv_obj_t *a_ui_rect_create(const a_ui_rect_cfg_t *cfg);

// --- Panel ------------------------------------------------------------------
typedef struct {
    lv_obj_t *parent; // NULL → active screen
    int32_t w, h;     // 0 → fill parent
    int32_t x, y;
    bool transparent; // skip theme chrome (no border/bg)
    bool scrollable;  // default false
} a_ui_panel_cfg_t;

lv_obj_t *a_ui_panel_create(const a_ui_panel_cfg_t *cfg);

// --- Label ------------------------------------------------------------------
typedef struct {
    lv_obj_t *parent;
    const char *text;
    a_ui_role_t role;      // theme picks font + color
    lv_text_align_t align; // LV_TEXT_ALIGN_AUTO if 0
    int32_t x, y;
    int32_t w; // 0 → auto-size
} a_ui_label_cfg_t;

lv_obj_t *a_ui_label_create(const a_ui_label_cfg_t *cfg);
void a_ui_label_set_role(lv_obj_t *label, a_ui_role_t role);

// --- Button -----------------------------------------------------------------
typedef struct {
    lv_obj_t *parent;
    const char *text;
    int32_t x, y;
    int32_t w, h;
    lv_event_cb_t on_click; // optional
    void *user_data;        // optional
} a_ui_button_cfg_t;

lv_obj_t *a_ui_button_create(const a_ui_button_cfg_t *cfg);

// --- Indicator (LED-style dot) ---------------------------------------------
typedef struct {
    lv_obj_t *parent;
    int32_t x, y;
    int32_t diameter; // 0 → theme default
    a_ui_indicator_state_t state;
} a_ui_indicator_cfg_t;

lv_obj_t *a_ui_indicator_create(const a_ui_indicator_cfg_t *cfg);
void a_ui_indicator_set_state(lv_obj_t *ind, a_ui_indicator_state_t state);

// --- Splash -----------------------------------------------------------------
// Centers the active theme's splash bytes on parent. Returns NULL if the
// theme has no splash configured.
lv_obj_t *a_ui_splash_create(lv_obj_t *parent);

// --- Header bar -------------------------------------------------------------
// A horizontal status bar pinned to the top of `parent`. Children added to it
// stack right-to-left in insertion order: the first item appears at the far
// right edge, subsequent items appear to its left. Use a_ui_header_right_slot
// to fetch the container new items should be parented to.
typedef struct {
    lv_obj_t *parent; // NULL → active screen
    int32_t h;        // 0 → theme default (20)
} a_ui_header_cfg_t;

lv_obj_t *a_ui_header_create(const a_ui_header_cfg_t *cfg);
lv_obj_t *a_ui_header_right_slot(lv_obj_t *header);

// Re-parent `item` into the header's right slot and position it just to the
// left of any previously-added items. `item` should be sized before calling.
void a_ui_header_add_right(lv_obj_t *header, lv_obj_t *item);

// --- NTP clock --------------------------------------------------------------
// HH:MM label that auto-updates from atomic_ntp. Hidden whenever
// BIT_NTP_READY is unset; once synced, the colon blinks at 1 Hz.
lv_obj_t *a_ui_clock_create(lv_obj_t *parent);

// --- Wifi indicator ---------------------------------------------------------
// Clickable LV_SYMBOL_WIFI label. Color reflects BIT_WIFI_READY. Click
// opens the network selection screen (a_ui_wifi_menu_open).
lv_obj_t *a_ui_wifi_create(lv_obj_t *parent);

// Open the wifi network selection screen as a fullscreen modal. Closes when
// the user taps "Done" or saves credentials.
void a_ui_wifi_menu_open(void);

// --- Battery indicator ------------------------------------------------------
// Returns battery percentage in [0, 100], or a negative number if the device
// can't read battery (no GPIO/ADC wired). Projects with a battery should
// register a reader during init via a_ui_battery_set_reader().
typedef int (*a_ui_battery_reader_t)(void);

void a_ui_battery_set_reader(a_ui_battery_reader_t fn);

// Battery icon + percentage label. Hidden until a reader is registered and
// returns a non-negative value.
lv_obj_t *a_ui_battery_create(lv_obj_t *parent);

// --- Helpers ----------------------------------------------------------------
static inline void a_ui_show(lv_obj_t *o) { lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN); }
static inline void a_ui_hide(lv_obj_t *o) { lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); }

#ifdef __cplusplus
}
#endif
