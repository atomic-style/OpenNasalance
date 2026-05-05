#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#define BIT_INIT BIT0
#define BIT_EVENTS_INIT BIT1
#define BIT_NETIF_INIT BIT2
#define BIT_WIFI_ENABLE BIT3
#define BIT_WIFI_INIT BIT4
#define BIT_WIFI_START BIT5
#define BIT_WIFI_WAIT BIT6
#define BIT_WIFI_READY BIT7
#define BIT_WIFI_LOW_POWER BIT8
#define BIT_NET_TASK BIT9
#define BIT_NTP_ENABLE BIT10
#define BIT_NTP_INIT BIT11
#define BIT_NTP_READY BIT12
#define BIT_MQTT_ENABLE BIT13
#define BIT_MQTT_INIT BIT14
#define BIT_MQTT_READY BIT15
#define BIT_HA_ENABLE BIT16
#define BIT_HA_INIT BIT17
#define BIT_HA_READY BIT18
#define BIT_SD_READY BIT19
#define BIT_LCD_READY BIT20
#define BIT_LVGL_READY BIT21
#define BIT_LVGL_FS_READY BIT22
#define BIT_LED_READY BIT23

esp_err_t a_bits_init(void);
bool a_bits(EventBits_t bit);
void a_bits_set(EventBits_t bits);
void a_bits_wait(EventBits_t bit);
void a_bits_clear(EventBits_t bits);

/*
void atomic_bits_set(EventBits_t bits);
void atomic_bits_clear(EventBits_t bits);
void atomic_bits_wait(EventBits_t bit);
EventBits_t atomic_bits_get_all(void);
EventBits_t atomic_bits_get_bit(EventBits_t bit);
int atomic_bits_get_as_int(EventBits_t bit);
bool atomic_bits_get_as_bool(EventBits_t bit);
void atomic_bits_print(EventBits_t bit);
void atomic_bits_debug(void);
*/
