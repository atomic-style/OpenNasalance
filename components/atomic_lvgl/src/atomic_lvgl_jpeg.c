#include "atomic_lvgl_jpeg.h"

#include "atomic_bits.h"
#include "atomic_err.h"
#include "atomic_lcd.h"
#include "atomic_lcd_config.h"
#include "atomic_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "string.h"
#include <stdbool.h>

#define MAX_PATH_LENGTH 128
#define FAKE_LVGL_PATH "S:"

static const char *TAG = "atomic_lvgl_jpeg";
static a_lcd_t *s_a_lcd;

static char *s_path = NULL;
static lv_obj_t *s_jpeg = NULL;

static const char *a_lvgl_jpeg_prefix(void) {
    if (!s_a_lcd) {
        s_a_lcd = a_lcd_get();
        if (!s_a_lcd) {
            err(TAG, "LCD not initialized");
            return NULL;
        }
    }
    switch (s_a_lcd->id) {
    case LCD_W5:
        return "S:/gfx/480H/";
    case LCD_CYD:
        return "S:/gfx/320H/";
    case LCD_TDISPLAY:
        return "S:/gfx/170H/";
    case LCD_WS_TOUCH_LCD:
        return "S:/gfx/320H/";
    case LCD_RING:
        return "S:/gfx/466/";
    default:
        warn(TAG, "Board ID %d not explicitly supported, using default path", s_a_lcd->id);
        return "S:/gfx/";
    }
}

void a_lvgl_jpeg_cleanup(void) {
    if (s_jpeg) {
        // No lock needed if called from LVGL task, but safe to use anyway
        lv_lock();
        lv_obj_del(s_jpeg);
        lv_unlock();
        s_jpeg = NULL;
    }
    if (s_path) {
        free(s_path);
        s_path = NULL;
    }
}

// Internal function - must be called from LVGL task
// Exposed to atomic_lvgl.c for direct calls from LVGL task
esp_err_t a_lvgl_jpeg_internal(const char *filename) {
    if (!filename || filename[0] == '\0') {
        err(TAG, "Invalid filename");
        return ESP_ERR_INVALID_ARG;
    }

    int64_t start_time = esp_timer_get_time();

    a_lvgl_jpeg_cleanup();

    const char *prefix = a_lvgl_jpeg_prefix();
    if (!prefix) {
        err(TAG, "Failed to get path prefix");
        return ESP_ERR_INVALID_STATE;
    }

    size_t prefix_len = strlen(prefix);
    size_t filename_len = strlen(filename);
    if (prefix_len + filename_len >= MAX_PATH_LENGTH) {
        err(TAG, "Path too long: %zu + %zu >= %d", prefix_len, filename_len, MAX_PATH_LENGTH);
        return ESP_ERR_INVALID_ARG;
    }

    s_path = calloc(MAX_PATH_LENGTH, sizeof(char));
    if (!s_path) {
        err(TAG, "Failed to allocate path buffer");
        return ESP_ERR_NO_MEM;
    }

    int written = snprintf(s_path, MAX_PATH_LENGTH, "%s%s", prefix, filename);
    if (written < 0 || written >= MAX_PATH_LENGTH) {
        err(TAG, "Path construction failed");
        free(s_path);
        s_path = NULL;
        return ESP_ERR_INVALID_ARG;
    }

    debug(TAG, "Loading JPEG: %s", s_path);

    // No lock needed - we're already in LVGL task context
    s_jpeg = lv_image_create(lv_screen_active());
    if (!s_jpeg) {
        err(TAG, "Failed to create image object");
        free(s_path);
        s_path = NULL;
        return ESP_ERR_NO_MEM;
    }

    lv_image_set_src(s_jpeg, s_path);
    lv_obj_center(s_jpeg);

    int64_t load_time = esp_timer_get_time() - start_time;
    info(TAG, "JPEG loaded in %lld ms: %s", load_time / 1000, filename);

    return ESP_OK;
}

// Public API - queues request to LVGL task
esp_err_t a_lvgl_jpeg(const char *filename) {
    if (!filename || filename[0] == '\0') {
        err(TAG, "Invalid filename");
        return ESP_ERR_INVALID_ARG;
    }

    a_bits_wait(BIT_LVGL_READY);
    a_bits_wait(BIT_SD_READY);
    a_bits_wait(BIT_LVGL_FS_READY);

    // Get queue and task handle from atomic_lvgl
    extern QueueHandle_t s_jpeg_queue;
    extern TaskHandle_t s_lvgl_task_handle;

    if (!s_jpeg_queue) {
        err(TAG, "JPEG queue not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Check if we're already in the LVGL task
    if (s_lvgl_task_handle && xTaskGetCurrentTaskHandle() == s_lvgl_task_handle) {
        // Already in LVGL task, call directly
        return a_lvgl_jpeg_internal(filename);
    }

    // Queue the request for the LVGL task
    char *filename_copy = strdup(filename);
    if (!filename_copy) {
        err(TAG, "Failed to duplicate filename");
        return ESP_ERR_NO_MEM;
    }

    if (xQueueSend(s_jpeg_queue, &filename_copy, pdMS_TO_TICKS(1000)) != pdTRUE) {
        free(filename_copy);
        err(TAG, "Failed to queue JPEG request (queue full)");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}
