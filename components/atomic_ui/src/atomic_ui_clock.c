#include "atomic_ui_widgets.h"

#include "atomic_bits.h"
#include "atomic_log.h"
#include "atomic_ntp.h"
#include "atomic_ui_theme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdio.h>

static const char *TAG = "CLOCK";

// Half-second update drives both the minute refresh and the 1 Hz colon blink.
#define A_UI_CLOCK_TICK_MS 500
// Small stack: localtime_r + snprintf + lv_label_set_text fit comfortably.
#define A_UI_CLOCK_STACK 3072
// Lowest priority above idle: the clock must never preempt real work.
#define A_UI_CLOCK_PRIO (tskIDLE_PRIORITY + 1)

static void clock_task(void *arg) {
  lv_obj_t *label = (lv_obj_t *)arg;

  // Block here — using zero CPU — until NTP first syncs. After that we never
  // re-check the bit; a brief disconnection shouldn't blank the display.

  info(TAG, "atomic_ui_clock waiting for BIT_NTP_READY");
  a_bits_wait(BIT_NTP_READY);
  info(TAG, "atomic_ui_clock received BIT_NTP_READY -- creating clock.");

  lv_lock();
  lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
  lv_unlock();

  bool show_colon = true;
  TickType_t next = xTaskGetTickCount();
  for (;;) {
    char hms[12] = {0};
    atomic_ntp_hms(hms, sizeof hms);

    // hms is "HH:MM:SS"; render "HH:MM" with a controlled separator.
    char out[8];
    out[0] = hms[0];
    out[1] = hms[1];
    out[2] = show_colon ? ':' : ' ';
    out[3] = hms[3];
    out[4] = hms[4];
    out[5] = '\0';
    show_colon = !show_colon;

    lv_lock();
    lv_label_set_text(label, out);
    lv_unlock();

    vTaskDelayUntil(&next, pdMS_TO_TICKS(A_UI_CLOCK_TICK_MS));
  }
}

lv_obj_t *a_ui_clock_create(lv_obj_t *parent) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, a_ui_theme_role_font(A_UI_ROLE_STATUS), 0);
  lv_obj_set_style_text_color(label, a_ui_theme_role_color(A_UI_ROLE_BODY), 0);
  lv_label_set_text(label, "--:--");
  lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);

  xTaskCreate(clock_task, "ui_clock", A_UI_CLOCK_STACK, label, A_UI_CLOCK_PRIO,
              NULL);
  return label;
}
