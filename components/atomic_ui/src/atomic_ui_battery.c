// Battery indicator. Shows LV_SYMBOL_BATTERY_* + "NN%" when a project has
// registered a reader that returns >= 0. Hidden otherwise — projects without
// battery hardware never see it.

#include "atomic_ui_widgets.h"

#include "atomic_ui_theme.h"
#include "lvgl.h"
#include <stdio.h>

#define A_UI_BATTERY_TICK_MS 5000

static a_ui_battery_reader_t s_reader;

void a_ui_battery_set_reader(a_ui_battery_reader_t fn) { s_reader = fn; }

static const char *icon_for(int pct) {
  if (pct >= 88) return LV_SYMBOL_BATTERY_FULL;
  if (pct >= 63) return LV_SYMBOL_BATTERY_3;
  if (pct >= 38) return LV_SYMBOL_BATTERY_2;
  if (pct >= 13) return LV_SYMBOL_BATTERY_1;
  return LV_SYMBOL_BATTERY_EMPTY;
}

static lv_color_t color_for(int pct) {
  const a_ui_theme_t *t = a_ui_theme_get();
  if (pct < 15) return t->indicator_color[A_UI_IND_DANGER];
  if (pct < 30) return t->indicator_color[A_UI_IND_WARN];
  return t->role_color[A_UI_ROLE_BODY];
}

static void battery_tick(lv_timer_t *timer) {
  lv_obj_t *label = (lv_obj_t *)lv_timer_get_user_data(timer);
  if (!label) return;

  int pct = s_reader ? s_reader() : -1;
  if (pct < 0) {
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  if (pct > 100) pct = 100;
  lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);

  char buf[16];
  snprintf(buf, sizeof buf, "%s %d%%", icon_for(pct), pct);
  lv_label_set_text(label, buf);
  lv_obj_set_style_text_color(label, color_for(pct), 0);
}

lv_obj_t *a_ui_battery_create(lv_obj_t *parent) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, a_ui_theme_role_font(A_UI_ROLE_STATUS), 0);
  lv_obj_set_style_text_color(label, a_ui_theme_role_color(A_UI_ROLE_BODY), 0);
  lv_label_set_text(label, "");
  lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);

  lv_timer_create(battery_tick, A_UI_BATTERY_TICK_MS, label);
  return label;
}
