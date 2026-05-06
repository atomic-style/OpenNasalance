#pragma once

#include "atomic_net.h"
#include "esp_event.h"
#include "mqtt_client.h"

#define MAX_KV_PAIRS 8
#define MAX_KEY_LEN 32
#define MAX_VALUE_LEN 64

typedef struct a_mqtt_cfg_s {
    const char *uri;
    const char *username;
    const char *password;
    const char *topic;
    const char *client_id;
} a_mqtt_cfg_t;

// for decoding topic parts
typedef struct {
    char part1[64];
    char part2[64];
    char part3[64];
    char part4[64];
    int parts;
} mqtt_topic_parts_t;

// for decoding JSON payloads
typedef struct {
    char key[MAX_KEY_LEN];
    char value[MAX_VALUE_LEN];
} mqtt_kv_pair_t;

typedef struct {
    mqtt_kv_pair_t pairs[MAX_KV_PAIRS];
    int count;
} mqtt_payload_t;

typedef void (*atomic_mqtt_data_cb_t)(const char *topic, int topic_len, const char *data, int data_len);

int atomic_mqtt_send(const char *topic, const char *payload, int retain);
int atomic_mqtt_send_prefixed(const char *topic, const char *payload, int retain);
int atomic_mqtt_subscribe(const char *topic);
int atomic_mqtt_subscribe_prefixed(const char *topic);
esp_err_t atomic_mqtt_add_sub(const char *topic);
esp_err_t atomic_mqtt_set_data_cb(atomic_mqtt_data_cb_t cb);
esp_err_t atomic_mqtt_init(void);
esp_err_t a_mqtt_config(a_mqtt_cfg_t *mqtt_config);
const char *a_mqtt_get_topic(void);
