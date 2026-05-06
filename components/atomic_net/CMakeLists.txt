idf_component_register(
    SRCS
        "src/atomic_net.c"
        "src/atomic_wifi.c"
        "src/atomic_wifi_evt.c"
        "src/atomic_ntp.c"
        # MQTT and Home Assistant disabled in this build — depend on
        # espressif/mqtt, atomic_audio, atomic_sensors, atomic_json which
        # are not present in OpenNasalance. Re-add to SRCS to restore.
        # "src/atomic_mqtt.c"
        # "src/atomic_ha.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_netif
        esp_wifi
        esp_event
        esp_timer
        lwip
        atomic_util
        atomic_bits
        atomic_nvs
        # espressif__mqtt   # restore alongside atomic_mqtt.c
)
