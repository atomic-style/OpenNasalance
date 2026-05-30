// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "atomic_ui_widgets.h"
#include "atomic_err.h"
#include "atomic_log.h"
#include "atomic_ui_theme.h"
#include "lvgl.h"
#include <stddef.h>

static const char *TAG = "UI-W";

static lv_obj_t *resolve_parent(lv_obj_t *parent) {
  return parent ? parent : lv_screen_active();
}

static int32_t fill_or(int32_t v) { return v == 0 ? LV_PCT(100) : v; }

// ---------------------------------------------------------------------------
//  Simple primitives (explicit font / color / geometry)
// ---------------------------------------------------------------------------
lv_obj_t *a_ui_text_create(const a_ui_text_cfg_t *cfg) {
  lv_obj_t *l = lv_label_create(resolve_parent(cfg->parent));
  lv_label_set_text(l, cfg->text ? cfg->text : "");

  const lv_font_t *font =
      cfg->font ? cfg->font : a_ui_theme_role_font(A_UI_ROLE_BODY);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, cfg->color, 0);
  lv_obj_set_pos(l, cfg->x, cfg->y);
  return l;
}

lv_obj_t *a_ui_rect_create(const a_ui_rect_cfg_t *cfg) {
  lv_obj_t *r = lv_obj_create(resolve_parent(cfg->parent));
  lv_obj_remove_style_all(r);
  lv_obj_set_size(r, cfg->w, cfg->h);
  lv_obj_set_pos(r, cfg->x, cfg->y);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(r, cfg->color, 0);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
  return r;
}

// ---------------------------------------------------------------------------
//  Panel
// ---------------------------------------------------------------------------
lv_obj_t *a_ui_panel_create(const a_ui_panel_cfg_t *cfg) {
  const a_ui_theme_t *t = a_ui_theme_get();
  lv_obj_t *p = lv_obj_create(resolve_parent(cfg->parent));

  lv_obj_remove_style_all(p);
  lv_obj_set_size(p, fill_or(cfg->w), fill_or(cfg->h));
  lv_obj_set_pos(p, cfg->x, cfg->y);

  if (!cfg->scrollable)
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);

  if (!cfg->transparent) {
    lv_obj_set_style_bg_color(p, t->panel_bg, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(p, t->panel_border, 0);
    lv_obj_set_style_border_width(p, t->border_width, 0);
    lv_obj_set_style_radius(p, t->corner_rad, 0);
    lv_obj_set_style_pad_all(p, t->panel_pad, 0);
  } else {
    lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
  }
  return p;
}

// ---------------------------------------------------------------------------
//  Label
// ---------------------------------------------------------------------------
lv_obj_t *a_ui_label_create(const a_ui_label_cfg_t *cfg) {
  lv_obj_t *l = lv_label_create(resolve_parent(cfg->parent));
  lv_label_set_text(l, cfg->text ? cfg->text : "");
  a_ui_label_set_role(l, cfg->role);

  if (cfg->align)
    lv_obj_set_style_text_align(l, cfg->align, 0);
  if (cfg->w) {
    lv_obj_set_width(l, cfg->w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  }
  lv_obj_set_pos(l, cfg->x, cfg->y);
  return l;
}

void a_ui_label_set_role(lv_obj_t *label, a_ui_role_t role) {
  lv_obj_set_style_text_font(label, a_ui_theme_role_font(role), 0);
  lv_obj_set_style_text_color(label, a_ui_theme_role_color(role), 0);
}

// ---------------------------------------------------------------------------
//  Button
// ---------------------------------------------------------------------------
lv_obj_t *a_ui_button_create(const a_ui_button_cfg_t *cfg) {
  const a_ui_theme_t *t = a_ui_theme_get();
  lv_obj_t *b = lv_button_create(resolve_parent(cfg->parent));

  if (cfg->w || cfg->h)
    lv_obj_set_size(b, cfg->w ? cfg->w : LV_SIZE_CONTENT,
                    cfg->h ? cfg->h : LV_SIZE_CONTENT);
  lv_obj_set_pos(b, cfg->x, cfg->y);

  lv_obj_set_style_bg_color(b, t->panel_bg, 0);
  lv_obj_set_style_bg_color(b, t->accent, LV_STATE_PRESSED);
  lv_obj_set_style_border_color(b, t->panel_border, 0);
  lv_obj_set_style_border_width(b, t->border_width, 0);
  lv_obj_set_style_radius(b, t->corner_rad, 0);
  lv_obj_set_style_text_color(b, t->fg, 0);
  lv_obj_set_style_text_color(b, t->bg, LV_STATE_PRESSED);
  lv_obj_set_style_text_font(b, a_ui_theme_role_font(A_UI_ROLE_BODY), 0);

  if (cfg->text) {
    lv_obj_t *lbl = lv_label_create(b);
    lv_label_set_text(lbl, cfg->text);
    lv_obj_center(lbl);
  }

  if (cfg->on_click) {
    lv_obj_add_event_cb(b, cfg->on_click, LV_EVENT_CLICKED, cfg->user_data);
  }
  return b;
}

// ---------------------------------------------------------------------------
//  Indicator (small filled circle)
// ---------------------------------------------------------------------------
lv_obj_t *a_ui_indicator_create(const a_ui_indicator_cfg_t *cfg) {
  const a_ui_theme_t *t = a_ui_theme_get();
  int32_t d = cfg->diameter > 0 ? cfg->diameter : t->indicator_diameter;

  lv_obj_t *i = lv_obj_create(resolve_parent(cfg->parent));
  lv_obj_remove_style_all(i);
  lv_obj_set_size(i, d, d);
  lv_obj_set_pos(i, cfg->x, cfg->y);
  lv_obj_set_style_radius(i, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(i, LV_OPA_COVER, 0);
  lv_obj_remove_flag(i, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(i, LV_OBJ_FLAG_CLICKABLE);

  a_ui_indicator_set_state(i, cfg->state);
  return i;
}

void a_ui_indicator_set_state(lv_obj_t *ind, a_ui_indicator_state_t state) {
  lv_obj_set_style_bg_color(ind, a_ui_theme_indicator_color(state), 0);
}

// ---------------------------------------------------------------------------
//  Splash
// ---------------------------------------------------------------------------

// Parse a JPEG's first SOF marker to extract image width/height. The TJPGD
// decoder's info_cb trusts whatever w/h is in the lv_image_dsc_t, so we must
// supply the real dimensions up front or the image widget reports 0x0 and
// never draws.
static bool jpeg_size(const uint8_t *data, size_t len, uint16_t *out_w,
                      uint16_t *out_h) {
  if (!data || len < 4) return false;
  if (data[0] != 0xFF || data[1] != 0xD8) return false; // SOI

  size_t i = 2;
  while (i + 3 < len) {
    if (data[i++] != 0xFF) return false;
    while (i < len && data[i] == 0xFF) i++; // 0xFF fill bytes
    if (i >= len) return false;
    uint8_t m = data[i++];
    if (m == 0x01 || (m >= 0xD0 && m <= 0xD7)) continue; // standalone, no len
    if (m == 0xD9) return false;                         // EOI before SOF
    if (i + 1 >= len) return false;
    uint16_t seg_len = (uint16_t)((data[i] << 8) | data[i + 1]);
    if (seg_len < 2) return false;
    // SOF markers: 0xC0-0xCF excluding DHT (C4), reserved (C8), DAC (CC).
    if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
      if (seg_len < 7 || i + 7 > len) return false;
      *out_h = (uint16_t)((data[i + 3] << 8) | data[i + 4]);
      *out_w = (uint16_t)((data[i + 5] << 8) | data[i + 6]);
      return true;
    }
    i += seg_len;
  }
  return false;
}

// ---------------------------------------------------------------------------
//  Header bar
// ---------------------------------------------------------------------------
//
// A horizontal bar pinned to the top of the screen. The "right slot" uses
// flex row-reverse + main-place START so each child added to it lands flush
// to the right edge, and subsequent children pack to the left of the previous
// one with a small column gap. Callers should create their widget with the
// slot as its parent — no reparenting required.

#define A_UI_HEADER_DEFAULT_H   20
#define A_UI_HEADER_RIGHT_PAD   4
#define A_UI_HEADER_ITEM_GAP    6

lv_obj_t *a_ui_header_create(const a_ui_header_cfg_t *cfg) {
  const a_ui_theme_t *t = a_ui_theme_get();
  lv_obj_t *p = resolve_parent(cfg->parent);
  int32_t h = cfg->h > 0 ? cfg->h : A_UI_HEADER_DEFAULT_H;

  lv_obj_t *bar = lv_obj_create(p);
  lv_obj_remove_style_all(bar);
  lv_obj_set_size(bar, LV_PCT(100), h);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_style_bg_color(bar, t->panel_bg, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(bar, t->panel_border, 0);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(bar, t->border_width, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);

  lv_obj_t *right = lv_obj_create(bar);
  lv_obj_remove_style_all(right);
  lv_obj_set_size(right, LV_PCT(100), LV_PCT(100));
  lv_obj_remove_flag(right, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_top(right, 0, 0);
  lv_obj_set_style_pad_bottom(right, 0, 0);
  lv_obj_set_style_pad_left(right, 0, 0);
  lv_obj_set_style_pad_right(right, A_UI_HEADER_RIGHT_PAD, 0);
  lv_obj_set_style_pad_column(right, A_UI_HEADER_ITEM_GAP, 0);

  // ROW + main-place END: items pack flush against the right edge of the slot
  // in child-index order. Last child added ends up rightmost.
  lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  return bar;
}

lv_obj_t *a_ui_header_right_slot(lv_obj_t *header) {
  if (!header || lv_obj_get_child_count(header) == 0) return NULL;
  return lv_obj_get_child(header, 0);
}

void a_ui_header_add_right(lv_obj_t *header, lv_obj_t *item) {
  lv_obj_t *slot = a_ui_header_right_slot(header);
  if (!slot || !item) return;
  lv_obj_set_parent(item, slot);
}

lv_obj_t *a_ui_splash_create(lv_obj_t *parent) {
  info(TAG, "a_ui_splash_create");
  const a_ui_theme_t *t = a_ui_theme_get();
  if (!t->splash_start || !t->splash_end || t->splash_end <= t->splash_start) {
    warn(TAG, "splash not found");
    return NULL;
  }
  lv_obj_t *p = resolve_parent(parent);

  size_t data_size = (size_t)(t->splash_end - t->splash_start);
  uint16_t w = 0, h = 0;
  if (!jpeg_size(t->splash_start, data_size, &w, &h)) {
    warn(TAG, "splash JPEG header parse failed");
    return NULL;
  }

  // Single shared descriptor: only one splash on screen at a time.
  static lv_image_dsc_t dsc;
  dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  dsc.header.cf = LV_COLOR_FORMAT_RAW;
  dsc.header.w = w;
  dsc.header.h = h;
  dsc.data_size = (uint32_t)data_size;
  dsc.data = t->splash_start;

  lv_obj_t *img = lv_image_create(p);
  lv_image_set_src(img, &dsc);
  lv_obj_center(img);
  return img;
}
