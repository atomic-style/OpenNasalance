// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Top-level UI lifecycle: brings up the active screen, shows splash + status,
// and exposes a_ui_clear() so init.c can dismiss them when the app takes over.

#include "atomic_ui.h"
#include "atomic_bits.h"
#include "atomic_err.h"
#include "atomic_log.h"
#include "atomic_nvs.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <string.h>

static const char *TAG = "UI";

#define A_UI_PROJECT_MAX 32

static lv_obj_t *s_root;
static lv_obj_t *s_splash;
static lv_obj_t *s_title;
static lv_obj_t *s_status;
static lv_obj_t *s_header;
static int32_t s_header_h;
static char s_project[A_UI_PROJECT_MAX];

static esp_err_t a_ui_init(void) {
  const a_ui_theme_t *t = a_ui_theme_get();
  lv_obj_t *scr = lv_screen_active();

  lv_obj_set_style_bg_color(scr, t->bg, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  s_root = a_ui_panel_create(&(a_ui_panel_cfg_t){
      .parent = scr,
      .transparent = true,
  });

  s_splash = a_ui_splash_create(s_root);

  int32_t h = lv_obj_get_height(s_root);
  s_status = a_ui_label_create(&(a_ui_label_cfg_t){
      .parent = s_root,
      .text = "loading...",
      .role = A_UI_ROLE_STATUS,
      .x = 4,
      .y = h - 16,
  });

  s_header = a_ui_header_create(&(a_ui_header_cfg_t){.parent = scr});
  s_header_h = lv_obj_get_height(s_header);
  lv_obj_move_foreground(s_header);

  s_title = lv_label_create(s_header);
  lv_obj_set_style_text_font(s_title, a_ui_theme_role_font(A_UI_ROLE_HEADER),
                             0);
  lv_obj_set_style_text_color(s_title, a_ui_theme_role_color(A_UI_ROLE_HEADER),
                              0);
  lv_label_set_text(s_title, s_project);
  lv_obj_align(s_title, LV_ALIGN_LEFT_MID, 4, 0);
  if (s_project[0] == '\0')
    lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);

  // Create header items directly inside the right slot. The slot uses
  // LV_FLEX_FLOW_ROW + main-place END, so items pack flush right in
  // child-index order — last added ends up rightmost.
  // Visual order (left → right): battery, wifi, clock.
  lv_obj_t *slot = a_ui_header_right_slot(s_header);
  a_ui_battery_create(slot);
  a_ui_wifi_create(slot);
  a_ui_clock_create(slot);

  lv_obj_update_layout(s_header);
  lv_obj_update_layout(s_root);
  return ESP_OK;
}

lv_obj_t *a_ui_header(void) { return s_header; }
int32_t a_ui_header_h(void) { return s_header_h; }

esp_err_t a_ui_clear(void) {
  lv_lock();
  if (s_splash)
    a_ui_hide(s_splash);
  // if (s_status)
  //   a_ui_hide(s_status);
  lv_obj_update_layout(s_root);
  lv_unlock();
  return ESP_OK;
}

// One-shot task: wait until the network is up in either STA or AP mode,
// then hide the splash. Avoids tying boot-completion to anything STA- or
// MQTT-specific that won't fire when the device boots as an Access Point.
static void splash_autoclear_task(void *arg) {
  (void)arg;
  a_bits_wait_any(BIT_WIFI_READY | BIT_WIFI_AP_READY);
  info(TAG, "network up — clearing splash");
  a_ui_clear();
  vTaskDelete(NULL);
}

esp_err_t a_ui_status(const char *text) {
  if (!text)
    return ESP_ERR_INVALID_ARG;
  lv_lock();
  if (s_status) {
    lv_label_set_text(s_status, text);
    a_ui_show(s_status);
  }
  lv_unlock();
  return ESP_OK;
}

void a_ui_project(const char *project) {
  if (project) {
    strncpy(s_project, project, sizeof s_project - 1);
    s_project[sizeof s_project - 1] = '\0';
  } else {
    s_project[0] = '\0';
  }
  if (!s_title)
    return;
  lv_lock();
  lv_label_set_text(s_title, s_project);
  if (s_project[0] == '\0') {
    lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(s_title, LV_OBJ_FLAG_HIDDEN);
  }
  lv_unlock();
}

esp_err_t atomic_ui(const char *project) {
  if (project) {
    strncpy(s_project, project, sizeof s_project - 1);
    s_project[sizeof s_project - 1] = '\0';
  } else {
    s_project[0] = '\0';
  }

  // Publish project name to NVS so other subsystems (e.g. wifi AP defaults)
  // can read it without depending on the project's config.h. Written every
  // boot so changes to DEV_PROJECT propagate immediately.
  if (s_project[0])
    a_nvs_set_str("dev.project", s_project);

  a_bits_wait(BIT_LVGL_READY);
  info(TAG, "atomic_ui (theme=%s, project=\"%s\")", a_ui_theme_get()->name,
       s_project);

  lv_lock();
  esp_err_t err = a_ui_init();
  try(a_ui_status("loading..."));
  lv_unlock();

  // Auto-dismiss the splash once the network is up in either mode. Uses a
  // small, low-priority task so the wait doesn't block init(). Stack has to
  // be large enough for a_ui_clear → lv_obj_update_layout, which is heavy.
  xTaskCreate(splash_autoclear_task, "ui_splash_clr", 4096, NULL,
              tskIDLE_PRIORITY + 1, NULL);
  return err;
}
