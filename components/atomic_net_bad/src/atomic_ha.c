#include "atomic_ha.h"
#include "atomic_bits.h"
#include "atomic_err.h"
#include "atomic_log.h"
#include "atomic_mqtt.h"
#include "atomic_sensors.h"
#include "esp_heap_caps.h"
#include <string.h>

#define TAG "atomic_ha"

#define HA_BUFFER_SIZE 4096

static const char *s_manufacturer = "atomic.style";
static const char *s_sw_version = "1.0.0";
static const char *s_hw_version = "1.0.0";

static const char *HA_MSG_AVAIL_ONLINE = "online";
static const char *HA_MSG_AVAIL_OFFLINE = "offline";

static a_ha_config_t s_ha_config = {0};

static char *s_ha_buffer = NULL;
static size_t s_ha_buffer_len = 0;

static char *s_topic_availability = NULL;
static char *s_topic_state = NULL;
static char *s_topic_discovery = NULL;
static char *s_topic_cmd = NULL;

static esp_err_t a_ha_config_topics(void) {
  // info(TAG, "ha_config_topics()");
  if (!s_ha_config.mqtt_topic || !s_ha_config.device_unit) {
    err(TAG, "MQTT topic or device unit not set");
    return ESP_ERR_INVALID_ARG;
  }

  info(TAG, "mqtt_topic: %s", s_ha_config.mqtt_topic);
  // info(TAG, "device_unit: %s", s_ha_config.device_unit);

  uint8_t topic_max_len = 64;
  if (strlen(s_ha_config.mqtt_topic) + strlen(s_ha_config.device_unit) + 12 >
      topic_max_len) {
    err(TAG, "MQTT topic or device unit too long");
    return ESP_ERR_INVALID_ARG;
  }

  s_topic_availability =
      heap_caps_malloc(topic_max_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_topic_state =
      heap_caps_malloc(topic_max_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_topic_discovery =
      heap_caps_malloc(topic_max_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_topic_cmd =
      heap_caps_malloc(topic_max_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  snprintf(s_topic_availability, topic_max_len, "%s/device/%s/availability",
           s_ha_config.mqtt_topic, s_ha_config.device_unit);
  snprintf(s_topic_state, topic_max_len, "%s/device/%s/state",
           s_ha_config.mqtt_topic, s_ha_config.device_unit);
  snprintf(s_topic_discovery, topic_max_len, "%s/device/%s/config",
           s_ha_config.mqtt_topic, s_ha_config.device_unit);
  snprintf(s_topic_cmd, topic_max_len, "%s/device/%s/cmd",
           s_ha_config.mqtt_topic, s_ha_config.device_unit);

  return ESP_OK;
}

static esp_err_t ha_buffer_append(const char *str) {
  size_t str_len = strlen(str);
  if (s_ha_buffer_len + str_len >= HA_BUFFER_SIZE - 1) {
    warn(TAG, "Buffer overflow: s_ha_buffer_len=%d, str_len=%d",
         s_ha_buffer_len, str_len);
    return ESP_ERR_NO_MEM;
  }
  memcpy(s_ha_buffer + s_ha_buffer_len, str, str_len);
  s_ha_buffer[s_ha_buffer_len + str_len] = '\0';
  s_ha_buffer_len += str_len;
  return ESP_OK;
}

static void ha_buffer_append_json(const char *key, const char *value,
                                  bool comma) {
  ha_buffer_append("\"");
  ha_buffer_append(key);
  ha_buffer_append("\": \"");
  ha_buffer_append(value);
  ha_buffer_append("\"");
  if (comma)
    ha_buffer_append(",");
}

static esp_err_t ha_buffer_clear(void) {
  if (!s_ha_buffer) {
    s_ha_buffer =
        heap_caps_malloc(HA_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_ha_buffer) {
      err(TAG, "Failed to allocate HA buffer from SPIRAM");
      return ESP_ERR_NO_MEM;
    }
  }
  memset(s_ha_buffer, 0, HA_BUFFER_SIZE);
  s_ha_buffer_len = 0;
  return ESP_OK;
}

static esp_err_t a_ha_publish_discovery(void) {
  try(ha_buffer_clear());

  // device
  ha_buffer_append("{\"dev\":{");
  ha_buffer_append_json("ids", s_ha_config.device_unit, true); // identifiers
  ha_buffer_append_json("name", s_ha_config.device_name, true);
  ha_buffer_append_json("mdl", s_ha_config.device_model, true); // model
  ha_buffer_append_json("mf", s_manufacturer, true);            // manufacturer
  ha_buffer_append_json("sw", s_sw_version, true);              // sw_version
  ha_buffer_append_json("hw", s_hw_version, false);             // hw_version

  // origin
  ha_buffer_append("}, \"o\":{");
  ha_buffer_append_json("name", s_ha_config.device_unit, true);
  ha_buffer_append_json("sw_version", s_sw_version, false);
  ha_buffer_append("},");

  // availability
  ha_buffer_append_json("availability_topic", s_topic_availability, true);
  ha_buffer_append_json("payload_available", HA_MSG_AVAIL_ONLINE, true);
  ha_buffer_append_json("payload_not_available", HA_MSG_AVAIL_OFFLINE, true);

  // components
  if (a_sensors_count() > 0) {
    ha_buffer_append("\"components\": {");
    for (int i = 0; i < a_sensors_count(); i++) {
      const a_sensor_t *sensor = a_sensors_get(i);
      ha_buffer_append("\"");
      ha_buffer_append(sensor->name);
      ha_buffer_append("\": {");
      ha_buffer_append_json("platform", sensor->platform, true);
      if (sensor->device_class)
        ha_buffer_append_json("device_class", sensor->device_class, true);
      char unique_id[128];
      snprintf(unique_id, sizeof(unique_id), "%s_%s", s_ha_config.device_unit,
               sensor->unique_id);
      ha_buffer_append_json("unique_id", unique_id, true);
      if (sensor->unit)
        ha_buffer_append_json("unit_of_measurement", sensor->unit, true);
      if (sensor->icon)
        ha_buffer_append_json("icon", sensor->icon, true);
      if (sensor->payload_on)
        ha_buffer_append_json("payload_on", sensor->payload_on, true);
      if (sensor->payload_off)
        ha_buffer_append_json("payload_off", sensor->payload_off, true);
      char value_template[128];
      snprintf(value_template, sizeof(value_template), "{{ value_json.%s }}",
               sensor->unique_id);
      ha_buffer_append_json("value_template", value_template, false);
      ha_buffer_append("}");
      if (i < a_sensors_count() - 1)
        ha_buffer_append(", ");
    }
    ha_buffer_append("},");
  }

  // state_topic and command_topic
  ha_buffer_append_json("state_topic", s_topic_state, true);
  ha_buffer_append_json("command_topic", s_topic_cmd, false);
  ha_buffer_append("}");
  info(TAG, "Publishing discovery to %s", s_topic_discovery);
  int msg_id = atomic_mqtt_send(s_topic_discovery, s_ha_buffer, 0);
  return (msg_id > -1) ? ESP_OK : ESP_FAIL;
}

static esp_err_t a_ha_publish_availability(void) {
  int msg_id = atomic_mqtt_send(s_topic_availability, HA_MSG_AVAIL_ONLINE, 1);
  return (msg_id > -1) ? ESP_OK : ESP_FAIL;
}

esp_err_t a_ha_publish_state(void) {
  if (!a_bits(BIT_MQTT_READY)) {
    warn(TAG, "a_ha_publish_state(): MQTT not ready");
    return ESP_ERR_INVALID_STATE;
  }
  if (!a_bits(BIT_HA_READY)) {
    warn(TAG, "a_ha_publish_state(): HA not ready");
    return ESP_ERR_INVALID_STATE;
  }
  try(ha_buffer_clear());
  ha_buffer_append("{");
  for (int i = 0; i < a_sensors_count(); i++) {
    const a_sensor_t *sensor = a_sensors_get(i);
    ha_buffer_append("\"");
    ha_buffer_append(sensor->unique_id);
    ha_buffer_append("\":");
    char value_buf[128] = {0};
    sensor->get_value(value_buf, sizeof(value_buf));
    ha_buffer_append(value_buf);
    if (i < a_sensors_count() - 1)
      ha_buffer_append(",");
  }
  ha_buffer_append("}");
  // info(TAG, "Publishing state to %s: %s", s_topic_state, s_ha_buffer);
  int msg_id = atomic_mqtt_send(s_topic_state, s_ha_buffer, 0);
  return (msg_id > -1) ? ESP_OK : ESP_FAIL;
}

esp_err_t a_ha_config(a_ha_config_t *config) {
  if (!config || !config->device_name || !config->device_unit ||
      !config->device_model || !config->device_chip_id) {
    return ESP_ERR_INVALID_ARG;
  }
  s_ha_config = *config;
  return ESP_OK;
}

esp_err_t a_ha_init(void) {
  // debug(TAG, "a_ha_init() called - waiting for MQTT ready");
  a_bits_wait(BIT_MQTT_READY);
  // debug(TAG, "MQTT ready - a_ha_init() proceeding");
  try(a_ha_config_topics());
  try(a_ha_publish_discovery());
  try(a_ha_publish_availability());
  a_bits_set(BIT_HA_READY);
  try(a_ha_publish_state());
  return ESP_OK;
}
