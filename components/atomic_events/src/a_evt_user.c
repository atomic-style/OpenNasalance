#include "a_evt_user.h"
#include "atomic_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

#define TAG "う"

// Event base definition
ESP_EVENT_DEFINE_BASE(A_EVT_USER_EVENTS);

// Global event loop handle
static esp_event_loop_handle_t s_user_event_loop = NULL;
static bool s_initialized = false;

// Handler registration structure
typedef struct {
    a_evt_user_handler_t user_handler;
    void *user_arg;
} handler_wrapper_t;

static void a_evt_user_default_handler(void *handler_arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    if (base != A_EVT_USER_EVENTS) {
        return;
    }

    a_evt_user_data_t *data = (a_evt_user_data_t *)event_data;
    if (data == NULL) {
        warn(TAG, "Received NULL event data");
        return;
    }

    debug(TAG, "Event received: id=%ld, condition=%d, severity=%d, from=%d, to=%d", event_id, data->condition,
          data->severity, data->from, data->to);

    // Default handler - can be extended with lookup tables later
    // For now, this is a placeholder that modules can register handlers with
}

// Wrapper function to convert esp_event_handler signature to a_evt_user_handler_t
static void handler_wrapper(void *handler_arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    if (base != A_EVT_USER_EVENTS) {
        return;
    }

    handler_wrapper_t *wrapper = (handler_wrapper_t *)handler_arg;
    if (wrapper == NULL || wrapper->user_handler == NULL) {
        return;
    }

    a_evt_user_data_t *data = (a_evt_user_data_t *)event_data;
    if (data == NULL) {
        return;
    }

    // Call the user's handler with the simplified signature
    wrapper->user_handler(data, wrapper->user_arg);
}

esp_err_t a_evt_user_init(void) {
    if (s_initialized) {
        warn(TAG, "a_evt_user_init() already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    debug(TAG, "a_evt_user_init()");

    // Configure event loop with dedicated task
    esp_event_loop_args_t loop_args = {.queue_size = 10,
                                       .task_name = "evt_user_task",
                                       .task_priority = uxTaskPriorityGet(NULL),
                                       .task_stack_size = 3072,
                                       .task_core_id = tskNO_AFFINITY};

    esp_err_t ret = esp_event_loop_create(&loop_args, &s_user_event_loop);
    if (ret != ESP_OK) {
        err(TAG, "Failed to create user event loop: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register default handler for all events
    ret = esp_event_handler_instance_register_with(s_user_event_loop, A_EVT_USER_EVENTS, ESP_EVENT_ANY_ID,
                                                   a_evt_user_default_handler, NULL, NULL);
    if (ret != ESP_OK) {
        err(TAG, "Failed to register default handler: %s", esp_err_to_name(ret));
        esp_event_loop_delete(s_user_event_loop);
        s_user_event_loop = NULL;
        return ret;
    }

    s_initialized = true;
    info(TAG, "User event loop initialized");
    return ESP_OK;
}

esp_err_t a_evt_user_post(a_evt_user_id_t event_id, const a_evt_user_data_t *event_data, int timeout_ms) {
    if (!s_initialized || s_user_event_loop == NULL) {
        err(TAG, "User event loop not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (event_data == NULL) {
        err(TAG, "Event data cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    TickType_t timeout_ticks = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    esp_err_t ret = esp_event_post_to(s_user_event_loop, A_EVT_USER_EVENTS, event_id, event_data,
                                      sizeof(a_evt_user_data_t), timeout_ticks);
    if (ret != ESP_OK) {
        err(TAG, "Failed to post event: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t a_evt_user_register_handler(a_evt_user_id_t event_id, a_evt_user_handler_t handler, void *handler_arg) {
    if (!s_initialized || s_user_event_loop == NULL) {
        err(TAG, "User event loop not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (handler == NULL) {
        err(TAG, "Handler cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Allocate wrapper structure (caller is responsible for cleanup if needed)
    // For simplicity, we'll allocate it and it will persist
    handler_wrapper_t *wrapper = malloc(sizeof(handler_wrapper_t));
    if (wrapper == NULL) {
        err(TAG, "Failed to allocate handler wrapper");
        return ESP_ERR_NO_MEM;
    }

    wrapper->user_handler = handler;
    wrapper->user_arg = handler_arg;

    int32_t id = (event_id == A_EVT_USER_ANY) ? ESP_EVENT_ANY_ID : event_id;

    esp_err_t ret = esp_event_handler_instance_register_with(s_user_event_loop, A_EVT_USER_EVENTS, id, handler_wrapper,
                                                             wrapper, NULL);
    if (ret != ESP_OK) {
        err(TAG, "Failed to register handler: %s", esp_err_to_name(ret));
        free(wrapper);
        return ret;
    }

    debug(TAG, "Handler registered for event_id=%ld", event_id);
    return ESP_OK;
}

esp_event_loop_handle_t a_evt_user_get_loop(void) { return s_user_event_loop; }