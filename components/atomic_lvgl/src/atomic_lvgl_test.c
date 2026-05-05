// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "atomic_lvgl_test.h"

#include "atomic_log.h"
#include "core/lv_obj_pos.h"
#include "esp_err.h"
#include "lvgl.h"

static const char *TAG = "LVGL Test";

void atomic_fucking_around_with_lcd_screen_graphics_and_fuck_up_the_graphics_library(void) {
    lv_lock();
    lv_obj_t *scr = lv_screen_active();
    lv_obj_t *root = lv_obj_create(scr);
    uint32_t h_res = lv_obj_get_width(scr);
    uint32_t v_res = lv_obj_get_height(scr);
    info(TAG, "h_res: %d, v_res: %d", h_res, v_res);

    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, h_res, v_res);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root, lv_color_make(32, 0, 64), 0);

    lv_obj_set_style_border_width(root, 2, 0);
    lv_obj_set_style_border_color(root, lv_color_make(255, 0, 0), 0);

    // horizontal line
    lv_obj_t *hline = lv_obj_create(root);
    lv_obj_remove_style_all(hline);
    lv_obj_set_size(hline, h_res, 2);
    lv_obj_set_pos(hline, 0, v_res / 2 - 1);
    lv_obj_set_style_bg_opa(hline, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(hline, lv_color_make(255, 255, 255), 0);

    lv_obj_t *vline = lv_obj_create(root);
    lv_obj_remove_style_all(vline);
    lv_obj_set_size(vline, 2, v_res);
    lv_obj_set_pos(vline, h_res / 2 - 1, 0);
    lv_obj_set_style_bg_opa(vline, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(vline, lv_color_make(255, 255, 255), 0);

    // top left box
    lv_obj_t *tl = lv_obj_create(root);
    lv_obj_remove_style_all(tl);
    lv_obj_set_size(tl, 16, 16);
    lv_obj_set_pos(tl, 1, 1);
    lv_obj_set_style_bg_opa(tl, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(tl, lv_color_make(255, 255, 0), 0);

    // top right box
    lv_obj_t *tr = lv_obj_create(root);
    lv_obj_remove_style_all(tr);
    lv_obj_set_size(tr, 16, 16);
    lv_obj_set_pos(tr, h_res - 16, 1);
    lv_obj_set_style_bg_opa(tr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(tr, lv_color_make(0, 255, 255), 0);

    // Grid
    const int cols = 8;
    const int rows = 6;

    for (int i = 1; i < cols; i++) {
        lv_coord_t x = (lv_coord_t)((int64_t)h_res * i / cols);

        lv_obj_t *gl = lv_obj_create(root);
        lv_obj_remove_style_all(gl);
        lv_obj_set_size(gl, 1, v_res);
        lv_obj_set_pos(gl, x, 0);
        lv_obj_set_style_bg_opa(gl, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(gl, lv_color_make(64, 64, 64), 0);
    }

    for (int j = 1; j < rows; j++) {
        lv_coord_t y = (lv_coord_t)((int64_t)v_res * j / rows);

        lv_obj_t *gl = lv_obj_create(root);
        lv_obj_remove_style_all(gl);
        lv_obj_set_size(gl, h_res, 1);
        lv_obj_set_pos(gl, 0, y);
        lv_obj_set_style_bg_opa(gl, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(gl, lv_color_make(64, 64, 64), 0);
    }

    // Center box
    lv_obj_t *center_box = lv_obj_create(root);
    lv_obj_remove_style_all(center_box);
    lv_obj_set_size(center_box, 120, 60);
    lv_obj_set_style_bg_opa(center_box, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(center_box, lv_color_make(30, 30, 30), 0);
    lv_obj_set_style_border_width(center_box, 1, 0);
    lv_obj_set_style_border_color(center_box, lv_color_make(255, 255, 0), 0);
    lv_obj_center(center_box);

    // Labels
    lv_obj_t *lbl = lv_label_create(root);
    lv_obj_set_style_text_color(lbl, lv_color_make(0, 0, 235), 0);
    lv_label_set_text_fmt(lbl, "Disp: %dx%d  Center: (%d,%d)", (int)h_res, (int)v_res, (int)h_res / 2, (int)v_res / 2);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 24, 8);

    lv_obj_t *lbl2 = lv_label_create(root);
    lv_obj_set_style_text_color(lbl2, lv_color_make(235, 235, 235), 0);
    lv_label_set_text(lbl2, "CENTER BOX (120x60)\nshould be centered");
    lv_obj_center(lbl2);
    lv_obj_align(lbl2, LV_ALIGN_CENTER, 0, 50);

    lv_obj_update_layout(root);
    lv_unlock();
}