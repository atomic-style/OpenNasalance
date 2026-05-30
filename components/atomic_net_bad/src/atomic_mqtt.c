#include "atomic_mqtt.h"

#include <stdio.h>

#include "atomic_audio.h"
#include "atomic_bits.h"
#include "atomic_log.h"
#include "esp_err.h"
#include "esp_log.h"
#include "mqtt_client.h"

static const char *TAG = "atomic_mqtt";
static a_mqtt_cfg_t s_mqtt_config;
static esp_mqtt_client_handle_t s_mqtt_client;

#define ATOMIC_MQTT_MAX_SUBS 8
static const char *s_subs[ATOMIC_MQTT_MAX_SUBS];
static int s_sub_count = 0;
static atomic_mqtt_data_cb_t s_data_cb = NULL;

static const char *atomic_mqtt_last_will_topic(char *buf, size_t buf_len) {
    snprintf(buf, buf_len, "%s/%s/%s/%s", s_mqtt_config.topic, "device", s_mqtt_config.client_id, "availability");
    return buf;
}

static void atomic_mqtt_status(esp_err_t status) {
    (status == ESP_OK) ? a_bits_set(BIT_MQTT_READY) : a_bits_clear(BIT_MQTT_READY);
}

static inline const char *device_topic(char *buf, size_t buf_len, const char *subtopic) {
    snprintf(buf, buf_len, "%s/%s/%s", s_mqtt_config.topic, s_mqtt_config.client_id, subtopic);
    return buf;
}

static void atomic_mqtt_connected(void) {
    // atomic_mqtt_send_prefixed("status", "online", 1);
    atomic_mqtt_subscribe_prefixed("cmd/#");
    atomic_mqtt_subscribe("home/alert/#");
    for (int i = 0; i < s_sub_count; i++) {
        atomic_mqtt_subscribe(s_subs[i]);
    }
}

static void atomic_mqtt_parse_topic(char *restrict topic, mqtt_topic_parts_t *restrict result) {
    result->parts = 0;
    char *token = strtok(topic, "/");
    if (token) {
        strncpy(result->part1, token, sizeof(result->part1) - 1);
        result->parts++;
    }
    token = strtok(NULL, "/");
    if (token) {
        strncpy(result->part2, token, sizeof(result->part2) - 1);
        result->parts++;
    }
    token = strtok(NULL, "/");
    if (token) {
        strncpy(result->part3, token, sizeof(result->part3) - 1);
        result->parts++;
    }
    token = strtok(NULL, "/");
    if (token) {
        strncpy(result->part4, token, sizeof(result->part4) - 1);
        result->parts++;
    }
    result->part1[sizeof(result->part1) - 1] = '\0';
    result->part2[sizeof(result->part2) - 1] = '\0';
    result->part3[sizeof(result->part3) - 1] = '\0';
    result->part4[sizeof(result->part4) - 1] = '\0';
}

static void atomic_mqtt_parse_data(esp_mqtt_event_handle_t event) {
    char topic_buf[128] = {0};
    size_t topic_len = event->topic_len;
    if (topic_len >= sizeof(topic_buf))
        topic_len = sizeof(topic_buf) - 1;
    strncpy(topic_buf, event->topic, topic_len);
    topic_buf[topic_len] = '\0';

    mqtt_topic_parts_t topic_parts;
    atomic_mqtt_parse_topic(topic_buf, &topic_parts);

    if (strcmp(topic_parts.part2, "alert") == 0) {
        if (strcmp(topic_parts.part3, "motion") == 0) {
            atomic_audio_play("/sd/sfx/trek/autodef.wav");
        }
    }

    if (s_data_cb) {
        s_data_cb(event->topic, event->topic_len, event->data, event->data_len);
    }
}

static void atomic_mqtt_event(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        info(TAG, "MQTT Connected");
        atomic_mqtt_status(ESP_OK);
        atomic_mqtt_connected();
        break;
    case MQTT_EVENT_DISCONNECTED:
        warn(TAG, "MQTT Disconnected");
        atomic_mqtt_status(ESP_FAIL);
        break;
    case MQTT_EVENT_DATA:
        info(TAG, "MQTT Data: %.*s\r\n %.*s\r\n", event->topic_len, event->topic, event->data_len, event->data);
        atomic_mqtt_parse_data(event);
        break;
    case MQTT_EVENT_ERROR:
        warn(TAG, "MQTT Error");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            esp_err_t sock_err = event->error_handle->esp_transport_sock_errno;
            if (sock_err != ESP_OK)
                err(TAG, "socket error: %s %d", strerror(sock_err), sock_err);
            esp_err_t esp_tls_err = event->error_handle->esp_tls_last_esp_err;
            if (esp_tls_err != ESP_OK)
                err(TAG, "esp-tls error: %d", esp_tls_err);
            esp_err_t esp_tls_stack_err = event->error_handle->esp_tls_stack_err;
            if (esp_tls_stack_err != ESP_OK)
                err(TAG, "tls stack error: %d", esp_tls_stack_err);
            esp_err_t esp_transport_err = event->error_handle->esp_transport_sock_errno;
            if (esp_transport_err != ESP_OK)
                err(TAG, "transport error: %d", esp_transport_err);
        }
        atomic_mqtt_status(ESP_FAIL);
        break;
    default:
        // warn(TAG, "MQTT Unhandled event id:%d", event->event_id);
        break;
    }
}

static esp_err_t atomic_mqtt_start(void) {
    char last_will_topic[128] = {0};
    atomic_mqtt_last_will_topic(last_will_topic, sizeof(last_will_topic));
    info(TAG, "-- last_will_topic: %s", last_will_topic);
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = s_mqtt_config.uri,
        .credentials.client_id = s_mqtt_config.client_id,
        .credentials.username = s_mqtt_config.username,
        .credentials.authentication.password = s_mqtt_config.password,
        .session.last_will.topic = last_will_topic,
        .session.last_will.msg = "offline",
        .session.last_will.msg_len = strlen("offline"),
        .session.last_will.retain = 1,
        .session.keepalive = 5,
    };
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT client init failed");
        return ESP_FAIL;
    }
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, atomic_mqtt_event, NULL);
    esp_mqtt_client_start(s_mqtt_client);
    return ESP_OK;
}

int atomic_mqtt_send(const char *topic, const char *payload, int retain) {
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, payload, strlen(payload), 0, retain);
    // ESP_LOGI(TAG, "sent, msg_id=%d, topic=%s, payload=%s", msg_id, topic, payload);
    return msg_id;
}

int atomic_mqtt_send_prefixed(const char *topic, const char *payload, int retain) {
    char topicbuf[128] = {0};
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, device_topic(topicbuf, sizeof(topicbuf), topic), payload,
                                         strlen(payload), 0, retain);
    // ESP_LOGI(TAG, "sent, msg_id=%d, topic=%s, payload=%s", msg_id, topicbuf, payload);
    return msg_id;
}

int atomic_mqtt_subscribe(const char *topic) {
    int msg_id = esp_mqtt_client_subscribe_single(s_mqtt_client, topic, 0);
    // ESP_LOGI(TAG, "subscribed: %s, msg_id=%d", topic, msg_id);
    return msg_id;
}

int atomic_mqtt_subscribe_prefixed(const char *topic) {
    char topicbuf[128] = {0};
    int msg_id = esp_mqtt_client_subscribe_single(s_mqtt_client, device_topic(topicbuf, sizeof(topicbuf), topic), 0);
    // ESP_LOGI(TAG, "subscribed: %s, msg_id=%d", topicbuf, msg_id);
    return msg_id;
}

esp_err_t atomic_mqtt_add_sub(const char *topic) {
    if (!topic) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_sub_count >= ATOMIC_MQTT_MAX_SUBS) {
        return ESP_ERR_NO_MEM;
    }
    s_subs[s_sub_count++] = topic;
    return ESP_OK;
}

esp_err_t atomic_mqtt_set_data_cb(atomic_mqtt_data_cb_t cb) {
    s_data_cb = cb;
    return ESP_OK;
}

esp_err_t atomic_mqtt_init(void) {
    debug(TAG, "atomic_mqtt_init()");
    a_bits_wait(BIT_MQTT_INIT);
    return atomic_mqtt_start();
}

esp_err_t a_mqtt_config(a_mqtt_cfg_t *mqtt_config) {
    s_mqtt_config = *mqtt_config;
    a_bits_set(BIT_MQTT_INIT);
    return ESP_OK;
}

const char *a_mqtt_get_topic(void) { return s_mqtt_config.topic; }

/*
// Alert event posting functions
void atomic_mqtt_post_motion_alert(const char *source, const char *message, const char *payload)
{
    atomic_alert_event_data_t event_data = {
        .type = ATOMIC_ALERT_EVENT_MOTION,
        .source = source,
        .message = message,
        .payload = payload
    };
    esp_err_t ret = esp_event_post(ATOMIC_ALERT_EVENTS, ATOMIC_ALERT_EVENT_MOTION, &event_data, sizeof(event_data),
portMAX_DELAY); if (ret != ESP_OK) { ESP_LOGE(TAG, "Failed to post motion alert event: %s", esp_err_to_name(ret)); }
else { ESP_LOGI(TAG, "Posted motion alert event successfully");
    }
}

void atomic_mqtt_post_sound_alert(const char *source, const char *message, const char *payload)
{
    atomic_alert_event_data_t event_data = {
        .type = ATOMIC_ALERT_EVENT_SOUND,
        .source = source,
        .message = message,
        .payload = payload
    };
    esp_event_post(ATOMIC_ALERT_EVENTS, ATOMIC_ALERT_EVENT_SOUND, &event_data, sizeof(event_data), portMAX_DELAY);
    ESP_LOGI(TAG, "Posted sound alert event");
}

void atomic_mqtt_post_temperature_alert(const char *source, const char *message, const char *payload)
{
    atomic_alert_event_data_t event_data = {
        .type = ATOMIC_ALERT_EVENT_TEMPERATURE,
        .source = source,
        .message = message,
        .payload = payload
    };
    esp_event_post(ATOMIC_ALERT_EVENTS, ATOMIC_ALERT_EVENT_TEMPERATURE, &event_data, sizeof(event_data), portMAX_DELAY);
    ESP_LOGI(TAG, "Posted temperature alert event");
}

void atomic_mqtt_post_custom_alert(const char *source, const char *message, const char *payload)
{
    atomic_alert_event_data_t event_data = {
        .type = ATOMIC_ALERT_EVENT_CUSTOM,
        .source = source,
        .message = message,
        .payload = payload
    };
    esp_event_post(ATOMIC_ALERT_EVENTS, ATOMIC_ALERT_EVENT_CUSTOM, &event_data, sizeof(event_data), portMAX_DELAY);
    ESP_LOGI(TAG, "Posted custom alert event");
}
*/