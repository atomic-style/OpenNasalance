#include "atomic_lvgl_clock.h"

#include <stdio.h>
#include <time.h>

#include "atomic_bits.h"
#include "atomic_lcd.h"
#include "atomic_lvgl.h"
#include "atomic_ntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

//static const char *TAG = "atomic_lvgl_clock";

LV_FONT_DECLARE(lcars48);
#define USE_SCALE

static void atomic_lvgl_clock_task(void *arg)
{
	time_t	  c_time;
	struct tm c_tm;
	char	  c_str[9] = {0};
	sprintf(c_str, "%s", "88:88:88");

	lv_lock();
	lv_obj_clean(lv_scr_act());

	lv_coord_t screen_w = lv_disp_get_hor_res(NULL);
	lv_coord_t screen_h = lv_disp_get_ver_res(NULL);

	// const lv_font_t *fnt = &lv_font_unscii_16;

	lv_color_t col_blue = lv_color_make(0, 0, 128);

	lv_obj_t *clock_bk = lv_obj_create(lv_scr_act());
	lv_obj_set_size(clock_bk, screen_w, screen_h);
	lv_obj_set_style_border_color(clock_bk, col_blue, 0);
	lv_obj_set_style_bg_color(clock_bk, col_blue, 0);
	lv_obj_align(clock_bk, LV_ALIGN_CENTER, 0, 0);

	lv_obj_t *c_label = lv_label_create(clock_bk);
	lv_obj_set_style_text_font(c_label, &lcars48, LV_PART_MAIN);
	lv_obj_set_style_text_color(c_label, lv_color_white(), 0);
	// lv_obj_set_style_text_font(c_label, lcars24, 0);
	lv_obj_set_style_bg_color(c_label, col_blue, 0);
	lv_label_set_text(c_label, c_str);

#ifdef USE_SCALE
	static lv_style_t style_large;
	lv_style_init(&style_large);
	lv_style_set_text_font(&style_large, &lv_font_unscii_16);
	lv_style_set_text_letter_space(&style_large, 1);
	lv_style_set_text_line_space(&style_large, 2);
	lv_style_set_transform_zoom(&style_large, 512);
	lv_obj_add_style(c_label, &style_large, 0);
#endif

	lv_obj_update_layout(c_label);

#ifdef USE_SCALE
	lv_coord_t label_w = lv_obj_get_width(c_label);
	lv_coord_t label_h = lv_obj_get_height(c_label);
	lv_style_set_transform_pivot_x(&style_large, label_w / 2);
	lv_style_set_transform_pivot_y(&style_large, label_h / 2);
#endif
	lv_obj_center(c_label);

	lv_unlock();

	while (1) {
		time(&c_time);
		localtime_r(&c_time, &c_tm);
		char c_sep = (c_tm.tm_sec % 2 == 0) ? ':' : ' ';
		snprintf(c_str, sizeof(c_str), "%2d%c%02d%c%02d", c_tm.tm_hour, c_sep, c_tm.tm_min, c_sep, c_tm.tm_sec);
		lv_lock();
		lv_label_set_text(c_label, c_str);
		lv_unlock();
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}

esp_err_t atomic_lvgl_clock_init(void)
{
	a_bits_wait(BIT_NTP_READY);
	xTaskCreate(atomic_lvgl_clock_task, "atomic_lvgl_clock_task", 2048, NULL, (tskIDLE_PRIORITY + 1), NULL);
	return ESP_OK;
}
