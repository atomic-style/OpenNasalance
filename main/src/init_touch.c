#include "init_touch.h"

#include "atomic_err.h"
#include "atomic_log.h"
#include "atomic_touch.h"
#include "config.h"
#include "esp_err.h"
#include "lvgl.h"

static const char *TAG = "ス";

static lv_indev_t *s_indev = NULL;

static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    atomic_touch_point_t p;
    if (atomic_touch_read(&p) != ESP_OK) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    if (p.pressed) {
        data->point.x = p.x;
        data->point.y = p.y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

esp_err_t init_touch(void) {
    notice(TAG, "init_touch()");

    atomic_touch_cfg_t cfg = {
        .i2c_port = TOUCH_I2C_PORT,
        .pin_sda = PIN_TOUCH_SDA,
        .pin_scl = PIN_TOUCH_SCL,
        .pin_rst = PIN_TOUCH_RST,
        .pin_int = PIN_TOUCH_INT,
        .native_w = TOUCH_NATIVE_W,
        .native_h = TOUCH_NATIVE_H,
        .rotation_ccw = TOUCH_ROTATION_CCW,
    };
    try(atomic_touch_init(&cfg));

    lv_lock();
    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, lvgl_touch_read_cb);
    lv_unlock();

    info(TAG, "init_touch() done");
    return ESP_OK;
}
