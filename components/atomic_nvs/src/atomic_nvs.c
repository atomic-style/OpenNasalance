#include "atomic_nvs.h"
#include "atomic_err.h"
#include "atomic_log.h"
#include "atomic_nvs_debug.h"
#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "atomic_nvs";
static const char *s_namespace = "atomic";
static const uint8_t s_api_version = 1;
static nvs_handle_t a_handle;

static bool key_valid(const char *key) { return strlen(key) <= 15; }

static esp_err_t dump_value_for_entry(nvs_handle_t handle, const char *ns, const char *key, nvs_type_t type) {
    esp_err_t ok = ESP_OK;
    switch (type) {
    case NVS_TYPE_U8: {
        uint8_t v;
        try(nvs_get_u8(handle, key, &v));
        info(TAG, "NVS %s: key='%s' type=U8  val=%u", ns, key, v);
        break;
    }
    case NVS_TYPE_I8: {
        int8_t v;
        try(nvs_get_i8(handle, key, &v));
        info(TAG, "NVS %s: key='%s' type=I8  val=%d", ns, key, v);
        break;
    }
    case NVS_TYPE_U16: {
        uint16_t v;
        try(nvs_get_u16(handle, key, &v));
        info(TAG, "NVS %s: key='%s' type=U16 val=%u", ns, key, v);
        break;
    }
    case NVS_TYPE_I16: {
        int16_t v;
        try(nvs_get_i16(handle, key, &v));
        info(TAG, "NVS %s: key='%s' type=I16 val=%d", ns, key, v);
        break;
    }
    case NVS_TYPE_U32: {
        uint32_t v;
        try(nvs_get_u32(handle, key, &v));
        info(TAG, "NVS %s: key='%s' type=U32 val=%u", ns, key, v);
        break;
    }
    case NVS_TYPE_I32: {
        int32_t v;
        try(nvs_get_i32(handle, key, &v));
        info(TAG, "NVS %s: key='%s' type=I32 val=%d", ns, key, v);
        break;
    }
    case NVS_TYPE_U64: {
        uint64_t v;
        try(nvs_get_u64(handle, key, &v));
        info(TAG, "NVS %s: key='%s' type=U64 val=%llu", ns, key, (unsigned long long)v);
        break;
    }
    case NVS_TYPE_I64: {
        int64_t v;
        try(nvs_get_i64(handle, key, &v));
        info(TAG, "NVS %s: key='%s' type=I64 val=%lld", ns, key, (long long)v);
        break;
    }
    case NVS_TYPE_STR: {
        size_t len = 0;
        try(nvs_get_str(handle, key, NULL, &len));
        if (len < 128) {
            char buf[len];
            try(nvs_get_str(handle, key, buf, &len));
            info(TAG, "NVS %s: key='%s' type=STR len=%u val='%s'", ns, key, (unsigned)len, buf);
        } else {
            info(TAG, "NVS %s: key='%s' type=STR len=%u (too long to dump)", ns, key, (unsigned)len);
        }
        break;
    }
    case NVS_TYPE_BLOB: {
        size_t len = 0;
        try(nvs_get_blob(handle, key, NULL, &len));
        if (len < 128) {
            char buf[len];
            try(nvs_get_blob(handle, key, buf, &len));
            info(TAG, "NVS %s: key='%s' type=BLOB len=%u val='%s'", ns, key, (unsigned)len, (char *)buf);
        } else {
            info(TAG, "NVS %s: key='%s' type=BLOB len=%u (too long to dump)", ns, key, (unsigned)len);
        }
        break;
    }
    case NVS_TYPE_ANY: {
        size_t len = 0;
        try(nvs_get_blob(handle, key, NULL, &len));
        char buf[len];
        if (len < 128) {
            try(nvs_get_blob(handle, key, buf, &len));
            info(TAG, "NVS %s: key='%s' type=ANY len=%u val='%s'", ns, key, (unsigned)len, (char *)buf);
        } else {
            info(TAG, "NVS %s: key='%s' type=ANY len=%u (too long to dump)", ns, key, (unsigned)len);
        }
        break;
    }
    default:
        info(TAG, "NVS %s: key='%s' type=%d (not found)", ns, key, (int)type);
        break;
    }
    return ok;
}

static esp_err_t a_nvs_dump_all(void) {
    debug(TAG, "a_nvs_dump_all() ----------------------------------->");
    nvs_iterator_t it = NULL;
    esp_err_t err = nvs_entry_find("nvs", NULL, NVS_TYPE_ANY, &it);
    char current_ns[16] = "";
    nvs_handle_t ns_handle = 0;
    while (err == ESP_OK) {
        nvs_entry_info_t info_entry;
        nvs_entry_info(it, &info_entry);
        if (strcmp(current_ns, info_entry.namespace_name) != 0) {
            if (ns_handle) {
                nvs_close(ns_handle);
                ns_handle = 0;
            }
            strncpy(current_ns, info_entry.namespace_name, sizeof(current_ns));
            current_ns[sizeof(current_ns) - 1] = '\0';
            nvs_open(current_ns, NVS_READONLY, &ns_handle);
        }
        if (ns_handle)
            dump_value_for_entry(ns_handle, info_entry.namespace_name, info_entry.key, info_entry.type);
        err = nvs_entry_next(&it);
    }
    if (ns_handle)
        nvs_close(ns_handle);
    nvs_release_iterator(it);
    info(TAG, "------ END NVS FULL DUMP ------");
    return ESP_OK;
}

static esp_err_t a_nvs_check(void) {
    uint8_t a_version;
    esp_err_t ok = nvs_get_u8(a_handle, "api_version", &a_version);
    if (ok != ESP_OK) {
        ok = nvs_set_u8(a_handle, "api_version", s_api_version);
        ok = nvs_get_u8(a_handle, "api_version", &a_version);
    }
    if (ok != ESP_OK) {
        err(TAG, "nvs_get_u8() FAIL: %d - %s", ok, esp_err_to_name(ok));
        return ok;
    }
    // info(TAG, "API Version: %d", a_version);
    return ESP_OK;
}

static esp_err_t a_nvs_open(void) {
    // debug(TAG, "Opening NVS namespace: %s", s_namespace);
    esp_err_t ok = nvs_open(s_namespace, NVS_READWRITE, &a_handle);
    if (ok != ESP_OK) {
        err(TAG, "nvs_open() FAIL: %d - %s", ok, esp_err_to_name(ok));
        return ok;
    }
    return ok;
}

static esp_err_t a_nvs_stats(void) {
    nvs_stats_t nvs_stats;
    esp_err_t ok = nvs_get_stats(NULL, &nvs_stats);
    if (ok != ESP_OK) {
        err(TAG, "nvs_get_stats() FAIL: %d - %s", ok, esp_err_to_name(ok));
        return ok;
    }
    // info(TAG, "Used: %lu, Free: %lu, Available: %lu, All: %lu, Namespaces: %lu", nvs_stats.used_entries,
    //      nvs_stats.free_entries, nvs_stats.available_entries, nvs_stats.total_entries, nvs_stats.namespace_count);
    return ESP_OK;
}

static esp_err_t a_nvs_init_nvs(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        warn(TAG, "NVS read error. Erasing...");
        try(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        err(TAG, "NVS FAIL: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

esp_err_t a_nvs_get(const char *key, nvs_type_t type, void *value) {
    if (!key_valid(key)) {
        err(TAG, "Key too long");
        return ESP_ERR_INVALID_ARG;
    }
    switch (type) {
    case NVS_TYPE_U8:
        return nvs_get_u8(a_handle, key, (uint8_t *)value);
    case NVS_TYPE_I8:
        return nvs_get_i8(a_handle, key, (int8_t *)value);
    case NVS_TYPE_U16:
        return nvs_get_u16(a_handle, key, (uint16_t *)value);
    case NVS_TYPE_I16:
        return nvs_get_i16(a_handle, key, (int16_t *)value);
    case NVS_TYPE_U32:
        return nvs_get_u32(a_handle, key, (uint32_t *)value);
    case NVS_TYPE_I32:
        return nvs_get_i32(a_handle, key, (int32_t *)value);
    case NVS_TYPE_U64:
        return nvs_get_u64(a_handle, key, (uint64_t *)value);
    case NVS_TYPE_I64:
        return nvs_get_i64(a_handle, key, (int64_t *)value);
    default:
        err(TAG, "a_nvs_get() FAIL: %d - %s", ESP_ERR_INVALID_ARG, "Invalid type");
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t a_nvs_set(const char *key, nvs_type_t type, void *value) {
    if (!key_valid(key)) {
        err(TAG, "Key too long");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ok;

    switch (type) {
    case NVS_TYPE_U8:
        ok = nvs_set_u8(a_handle, key, *(uint8_t *)value);
        break;
    case NVS_TYPE_I8:
        ok = nvs_set_i8(a_handle, key, *(int8_t *)value);
        break;
    case NVS_TYPE_U16:
        ok = nvs_set_u16(a_handle, key, *(uint16_t *)value);
        break;
    case NVS_TYPE_I16:
        ok = nvs_set_i16(a_handle, key, *(int16_t *)value);
        break;
    case NVS_TYPE_U32:
        ok = nvs_set_u32(a_handle, key, *(uint32_t *)value);
        break;
    case NVS_TYPE_I32:
        ok = nvs_set_i32(a_handle, key, *(int32_t *)value);
        break;
    case NVS_TYPE_U64:
        ok = nvs_set_u64(a_handle, key, *(uint64_t *)value);
        break;
    case NVS_TYPE_I64:
        ok = nvs_set_i64(a_handle, key, *(int64_t *)value);
        break;
    default:
        err(TAG, "a_nvs_set() FAIL: %d - %s", ESP_ERR_INVALID_ARG, "Invalid type");
        return ESP_ERR_INVALID_ARG;
    }

    if (ok != ESP_OK) {
        err(TAG, "nvs_set('%s') FAIL: %s", key, esp_err_to_name(ok));
        return ok;
    }

    return nvs_commit(a_handle);
}

esp_err_t a_nvs_get_i8(const char *key, int8_t *value) { return a_nvs_get(key, NVS_TYPE_I8, value); }
esp_err_t a_nvs_get_u8(const char *key, uint8_t *value) { return a_nvs_get(key, NVS_TYPE_U8, value); }
esp_err_t a_nvs_get_i16(const char *key, int16_t *value) { return a_nvs_get(key, NVS_TYPE_I16, value); }
esp_err_t a_nvs_get_u16(const char *key, uint16_t *value) { return a_nvs_get(key, NVS_TYPE_U16, value); }
esp_err_t a_nvs_get_i32(const char *key, int32_t *value) { return a_nvs_get(key, NVS_TYPE_I32, value); }
esp_err_t a_nvs_get_u32(const char *key, uint32_t *value) { return a_nvs_get(key, NVS_TYPE_U32, value); }
esp_err_t a_nvs_get_i64(const char *key, int64_t *value) { return a_nvs_get(key, NVS_TYPE_I64, value); }
esp_err_t a_nvs_get_u64(const char *key, uint64_t *value) { return a_nvs_get(key, NVS_TYPE_U64, value); }

esp_err_t a_nvs_set_i8(const char *key, int8_t value) { return a_nvs_set(key, NVS_TYPE_I8, &value); }
esp_err_t a_nvs_set_u8(const char *key, uint8_t value) { return a_nvs_set(key, NVS_TYPE_U8, &value); }
esp_err_t a_nvs_set_i16(const char *key, int16_t value) { return a_nvs_set(key, NVS_TYPE_I16, &value); }
esp_err_t a_nvs_set_u16(const char *key, uint16_t value) { return a_nvs_set(key, NVS_TYPE_U16, &value); }
esp_err_t a_nvs_set_i32(const char *key, int32_t value) { return a_nvs_set(key, NVS_TYPE_I32, &value); }
esp_err_t a_nvs_set_u32(const char *key, uint32_t value) { return a_nvs_set(key, NVS_TYPE_U32, &value); }
esp_err_t a_nvs_set_i64(const char *key, int64_t value) { return a_nvs_set(key, NVS_TYPE_I64, &value); }
esp_err_t a_nvs_set_u64(const char *key, uint64_t value) { return a_nvs_set(key, NVS_TYPE_U64, &value); }

esp_err_t a_nvs_get_str(const char *key, char *value, size_t *value_len) {
    if (!key_valid(key)) {
        return ESP_ERR_INVALID_ARG;
    }
    return nvs_get_str(a_handle, key, value, value_len);
}

esp_err_t a_nvs_set_str(const char *key, const char *value) {
    if (!key_valid(key)) {
        return ESP_ERR_INVALID_ARG;
    }
    try(nvs_set_str(a_handle, key, value));
    try(nvs_commit(a_handle));
    return ESP_OK;
}

esp_err_t a_nvs_erase_key(const char *key) { return nvs_erase_key(a_handle, key); }

esp_err_t a_nvs_find_key(const char *key, nvs_type_t *type) { return nvs_find_key(a_handle, key, type); }

esp_err_t a_nvs_debug(void) {
    try(a_nvs_dump_all());
    return ESP_OK;
}

esp_err_t a_nvs_init(void) {
    debug(TAG, "a_nvs_init()");
    try(a_nvs_init_nvs());
    try(a_nvs_stats());
    try(a_nvs_open());
    try(a_nvs_check());
    return ESP_OK;
}
