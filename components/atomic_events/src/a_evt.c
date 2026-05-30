#include "a_evt.h"
#include "a_evt_user.h"
#include "atomic_bits.h"
#include "atomic_err.h"
#include "atomic_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "う"

ESP_EVENT_DEFINE_BASE(TASK_EVENTS);

static const char *get_id_string(esp_event_base_t base, int32_t id) {
  if (base == TASK_EVENTS) {
    switch (id) {
    // case TASK_EVENT_START: return "TASK_EVENT_START";
    // case TASK_EVENT_STOP:  return "TASK_EVENT_STOP";
    // case TASK_EVENT_PAUSE: return "TASK_EVENT_PAUSE";
    // case TASK_EVENT_RESUME:return "TASK_EVENT_RESUME";
    default:
      return "UNKNOWN_TASK_EVENT";
    }
  }
  return "UNKNOWN_EVENT_BASE";
}

static void a_evt(void *handler_args, esp_event_base_t base, int32_t id,
                  void *event_data) {
  // info(TAG, "a_evt %s %d", base, id);
}

static esp_err_t a_evt_register(void) {
  return esp_event_handler_instance_register(
      ESP_EVENT_ANY_BASE, ESP_EVENT_ANY_ID, a_evt, NULL, NULL);
}

esp_err_t a_evt_init(void) {
  if (a_bits(BIT_EVENTS_INIT)) {
    warn(TAG, "a_evt_init() already initialized.");
    return ESP_ERR_INVALID_STATE;
  }
  // debug(TAG, "a_evt_init()");
  esp_err_t ret = esp_event_loop_create_default();
  if (ret != ESP_OK) {
    err(TAG, "esp_event_loop_create_default() failed: %s",
        esp_err_to_name(ret));
    return ret;
  }
  try(a_evt_register());
  a_bits_set(BIT_EVENTS_INIT);
  return ESP_OK;
}
