#include "atomic_ntp.h"

#include "atomic_bits.h"
#include "atomic_log.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "sys/time.h"

static const char *TAG = "ㆥ";

// Dedicated to Morris Day

static a_ntp_cfg_t s_ntp_config;

#define NTP_IP_LEN 48
#define NTP_IP_COUNT 6
static const char *s_ntp_config_uri[NTP_IP_COUNT] = {"time-a-g.nist.gov", "time.google.com",     "time.windows.com",
                                                     "time.apple.com",    "time.cloudflare.com", "pool.ntp.org"};
static uint8_t s_ntp_config_id = 0;

static const char *s_ntp_config_tz = "EST5EDT,M3.2.0,M11.1.0";

static uint32_t s_ntp_config_timeout = 10000;
static uint32_t s_ntp_config_start = 0;
static bool s_ntp_wait = false;

uint64_t atomic_ntp_ms(void) { return esp_timer_get_time() / 1000; }

void atomic_ntp_hms(char *buf, size_t buflen) {
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    snprintf(buf, buflen, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
}

char *atomic_ntp_ts(void) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    return asctime(&timeinfo);
}

uint64_t a_ntp_ms(void) {
    if (!a_bits(BIT_NTP_READY))
        return 0;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t time_ms = (int64_t)tv.tv_sec * 1000LL + (int64_t)tv.tv_usec / 1000LL;
    return time_ms;
}

time_t atomic_time(void) {
    time_t now;
    time(&now);
    return now;
}

void atomic_ntp_print(void) {
    if (!a_bits(BIT_NTP_READY)) {
        ESP_LOGW(TAG, "Time is just a construct.");
        return;
    }
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "Current time: %s", strftime_buf);
}

void a_ntp_cb(struct timeval *tv) {
    s_ntp_wait = false;
    a_bits_set(BIT_NTP_READY);
    ESP_LOGI(TAG, "NTP Sync");
    atomic_ntp_print();
}

static void a_ntp_set_servername(void) {
    info(TAG, "NTP server[%u]: %s", s_ntp_config_id, s_ntp_config_uri[s_ntp_config_id]);
    esp_sntp_setservername(0, s_ntp_config_uri[s_ntp_config_id]);
}

static void a_ntp_tick(void) {
    int64_t t_ms = esp_timer_get_time() / 1000;
    debug(TAG, "a_ntp_tick() t_ms: %lld, s_ntp_config_start: %lld, s_ntp_config_timeout: %d", t_ms, s_ntp_config_start,
          s_ntp_config_timeout);
    if (t_ms - s_ntp_config_start > s_ntp_config_timeout) {
        warn(TAG, "NTP timeout");
        esp_sntp_stop();
        s_ntp_config_id++;
        if (s_ntp_config_id >= NTP_IP_COUNT) {
            s_ntp_config_id = 0;
        }
        a_ntp_set_servername();
        s_ntp_wait = false;
        return;
    }
}

static void a_ntp_connect(void) {
    if (s_ntp_wait) {
        a_ntp_tick();
        return;
    }
    info(TAG, "NTP connect -> %s", s_ntp_config_uri[s_ntp_config_id]);
    s_ntp_wait = true;
    s_ntp_config_start = esp_timer_get_time() / 1000;
    esp_sntp_init();
}

static void a_ntp_configure(void) {
    debug(TAG, "a_ntp_configure() tz: %s, uri: %s", s_ntp_config_tz, s_ntp_config_uri[s_ntp_config_id]);
    setenv("TZ", s_ntp_config_tz, 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_set_time_sync_notification_cb(a_ntp_cb);
    a_ntp_set_servername();
    a_bits_set(BIT_NTP_INIT);
}

void a_ntp_init(void) {
    if (!a_bits(BIT_NTP_INIT)) {
        a_ntp_configure();
    }
    a_ntp_connect();
}
