// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "atomic_http.h"

#include "atomic_bits.h"
#include "atomic_log.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "HTTP";

#define MAX_WS_CLIENTS 4
#define FRAME_HEADER 8 // type, ver, frame_idx_lo, frame_idx_hi, n_bins, amp_top, amp_bot, nasalance
#define FRAME_MAX_BYTES (FRAME_HEADER + 2 * A_HTTP_MAX_BINS)

static httpd_handle_t s_server = NULL;
static esp_timer_handle_t s_heartbeat_timer = NULL;
static const char *s_unit_name = "unknown";

static SemaphoreHandle_t s_clients_mu = NULL;
static int s_clients[MAX_WS_CLIENTS];
static int s_client_count = 0;

// Producer→consumer single-slot frame buffer. Producer (DSP task) overwrites
// last frame if consumer hasn't drained it yet — keeps the producer cheap and
// drops stale data instead of stale-sending it.
static SemaphoreHandle_t s_frame_mu = NULL;
static SemaphoreHandle_t s_frame_sem = NULL; // binary, signals broadcast task
static uint8_t s_frame_buf[FRAME_MAX_BYTES];
static size_t s_frame_len = 0;
static uint16_t s_frame_idx = 0;

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

// ---------------------------------------------------------------------------
// client list (protected by s_clients_mu)
// ---------------------------------------------------------------------------

static void clients_add(int fd) {
    xSemaphoreTake(s_clients_mu, portMAX_DELAY);
    for (int i = 0; i < s_client_count; i++) {
        if (s_clients[i] == fd) {
            xSemaphoreGive(s_clients_mu);
            return;
        }
    }
    if (s_client_count < MAX_WS_CLIENTS) {
        s_clients[s_client_count++] = fd;
        info(TAG, "ws client added fd=%d (total=%d)", fd, s_client_count);
    } else {
        warn(TAG, "ws client list full, rejecting fd=%d", fd);
    }
    xSemaphoreGive(s_clients_mu);
}

static void clients_remove(int fd) {
    xSemaphoreTake(s_clients_mu, portMAX_DELAY);
    for (int i = 0; i < s_client_count; i++) {
        if (s_clients[i] == fd) {
            s_clients[i] = s_clients[--s_client_count];
            info(TAG, "ws client removed fd=%d (total=%d)", fd, s_client_count);
            break;
        }
    }
    xSemaphoreGive(s_clients_mu);
}

static void clients_snapshot(int *out, int *out_count) {
    xSemaphoreTake(s_clients_mu, portMAX_DELAY);
    *out_count = s_client_count;
    memcpy(out, s_clients, s_client_count * sizeof(int));
    xSemaphoreGive(s_clients_mu);
}

// ---------------------------------------------------------------------------
// HTTP / handler — embedded index.html
// ---------------------------------------------------------------------------

static esp_err_t root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
}

static const httpd_uri_t s_root_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_handler,
    .user_ctx = NULL,
};

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------

static esp_err_t ws_send_text(int fd, const char *payload) {
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)payload,
        .len = strlen(payload),
    };
    return httpd_ws_send_frame_async(s_server, fd, &frame);
}

static esp_err_t ws_send_binary(int fd, const uint8_t *payload, size_t len) {
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_BINARY,
        .payload = (uint8_t *)payload,
        .len = len,
    };
    return httpd_ws_send_frame_async(s_server, fd, &frame);
}

static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        // handshake — register the client and send hello
        int fd = httpd_req_to_sockfd(req);
        clients_add(fd);
        char hello[96];
        snprintf(hello, sizeof(hello), "{\"type\":\"hello\",\"unit\":\"%s\"}", s_unit_name);
        ws_send_text(fd, hello);
        return ESP_OK;
    }

    // inbound frame — Phase 2 just drains / logs
    httpd_ws_frame_t frame = {0};
    esp_err_t ok = httpd_ws_recv_frame(req, &frame, 0);
    if (ok != ESP_OK) {
        warn(TAG, "ws_recv (len probe) FAIL: %s", esp_err_to_name(ok));
        return ok;
    }
    if (frame.len == 0 || frame.len > 256) return ESP_OK;

    uint8_t buf[257] = {0};
    frame.payload = buf;
    ok = httpd_ws_recv_frame(req, &frame, sizeof(buf) - 1);
    if (ok == ESP_OK && frame.type == HTTPD_WS_TYPE_TEXT) {
        debug(TAG, "ws rx: %.*s", (int)frame.len, buf);
    }
    return ok;
}

static const httpd_uri_t s_ws_uri = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = ws_handler,
    .user_ctx = NULL,
    .is_websocket = true,
};

static void ws_close_cb(httpd_handle_t hd, int sockfd) {
    clients_remove(sockfd);
    close(sockfd);
}

// ---------------------------------------------------------------------------
// DSP frame producer/consumer
// ---------------------------------------------------------------------------

void a_http_push_frame(const uint8_t *top_bins,
                       const uint8_t *bot_bins,
                       int n_bins,
                       uint8_t amp_top,
                       uint8_t amp_bot,
                       uint8_t nasalance_pct) {
    if (!s_frame_mu) return; // server not up yet
    if (n_bins <= 0 || n_bins > A_HTTP_MAX_BINS) return;
    if (!top_bins || !bot_bins) return;
    // Fast-path skip if no clients are listening — no point formatting bytes
    // we'd just throw away. Read s_client_count without locking; a stale read
    // here is harmless (worst case: one frame too many or too few).
    if (s_client_count == 0) return;

    if (xSemaphoreTake(s_frame_mu, 0) != pdTRUE) return; // drop if contended

    size_t off = 0;
    s_frame_buf[off++] = 1;                       // type = nasometer frame
    s_frame_buf[off++] = 1;                       // version
    s_frame_buf[off++] = (uint8_t)(s_frame_idx & 0xFF);
    s_frame_buf[off++] = (uint8_t)((s_frame_idx >> 8) & 0xFF);
    s_frame_buf[off++] = (uint8_t)n_bins;
    s_frame_buf[off++] = amp_top;
    s_frame_buf[off++] = amp_bot;
    s_frame_buf[off++] = nasalance_pct;
    memcpy(s_frame_buf + off, top_bins, n_bins);
    off += n_bins;
    memcpy(s_frame_buf + off, bot_bins, n_bins);
    off += n_bins;
    s_frame_len = off;
    s_frame_idx++;

    xSemaphoreGive(s_frame_mu);
    xSemaphoreGive(s_frame_sem); // wake broadcast task; no-op if already given
}

static void broadcast_frame(void *arg) {
    static uint8_t local[FRAME_MAX_BYTES];
    size_t local_len;

    xSemaphoreTake(s_frame_mu, portMAX_DELAY);
    local_len = s_frame_len;
    if (local_len > 0) memcpy(local, s_frame_buf, local_len);
    s_frame_len = 0;
    xSemaphoreGive(s_frame_mu);
    if (local_len == 0) return;

    int fds[MAX_WS_CLIENTS];
    int n;
    clients_snapshot(fds, &n);
    for (int i = 0; i < n; i++) {
        esp_err_t ok = ws_send_binary(fds[i], local, local_len);
        if (ok != ESP_OK) {
            warn(TAG, "ws_send_binary fd=%d FAIL: %s — dropping", fds[i], esp_err_to_name(ok));
            clients_remove(fds[i]);
        }
    }
}

static void broadcast_task(void *arg) {
    while (1) {
        if (xSemaphoreTake(s_frame_sem, portMAX_DELAY) != pdTRUE) continue;
        // Run the actual sends inside the httpd task to keep socket access serialized
        // with handler/heartbeat traffic.
        if (s_server) httpd_queue_work(s_server, broadcast_frame, NULL);
    }
}

// ---------------------------------------------------------------------------
// Heartbeat — esp_timer fires every second, queues broadcast onto httpd task
// ---------------------------------------------------------------------------

static void broadcast_heartbeat(void *arg) {
    int fds[MAX_WS_CLIENTS];
    int n;
    clients_snapshot(fds, &n);
    if (n == 0) return;

    char msg[64];
    int64_t uptime_us = esp_timer_get_time();
    snprintf(msg, sizeof(msg), "{\"type\":\"heartbeat\",\"uptime\":%lld}", uptime_us / 1000000LL);

    for (int i = 0; i < n; i++) {
        esp_err_t ok = ws_send_text(fds[i], msg);
        if (ok != ESP_OK) {
            warn(TAG, "ws_send fd=%d FAIL: %s — dropping", fds[i], esp_err_to_name(ok));
            clients_remove(fds[i]);
        }
    }
}

static void heartbeat_tick(void *arg) {
    if (!s_server) return;
    // Run the actual send inside the httpd task to keep its socket access serialized.
    httpd_queue_work(s_server, broadcast_heartbeat, NULL);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

static void log_listen_addr(void) {
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!sta) {
        warn(TAG, "no STA netif — cannot report listen address");
        return;
    }
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(sta, &ip) != ESP_OK) {
        warn(TAG, "esp_netif_get_ip_info() failed");
        return;
    }
    info(TAG, "http server up at http://" IPSTR "/", IP2STR(&ip.ip));
}

static void http_start_task(void *arg) {
    a_bits_wait(BIT_WIFI_READY);

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;
    // CONFIG_LWIP_MAX_SOCKETS is 12 in defaults (3 reserved by httpd internally).
    // Keep the cap conservative until we measure under real WS load.
    cfg.max_open_sockets = 7;
    cfg.close_fn = ws_close_cb;

    esp_err_t ok = httpd_start(&s_server, &cfg);
    if (ok != ESP_OK) {
        err(TAG, "httpd_start() FAIL: %s", esp_err_to_name(ok));
        s_server = NULL;
        vTaskDelete(NULL);
        return;
    }

    httpd_register_uri_handler(s_server, &s_root_uri);
    httpd_register_uri_handler(s_server, &s_ws_uri);

    const esp_timer_create_args_t targs = {
        .callback = heartbeat_tick,
        .name = "http_hb",
    };
    esp_timer_create(&targs, &s_heartbeat_timer);
    esp_timer_start_periodic(s_heartbeat_timer, 1000000);

    log_listen_addr();
    vTaskDelete(NULL);
}

esp_err_t a_http_init(const char *unit_name) {
    if (s_server) {
        warn(TAG, "a_http_init() already called");
        return ESP_ERR_INVALID_STATE;
    }
    info(TAG, "a_http_init()");
    if (unit_name) s_unit_name = unit_name;
    s_clients_mu = xSemaphoreCreateMutex();
    s_frame_mu = xSemaphoreCreateMutex();
    s_frame_sem = xSemaphoreCreateBinary();
    if (!s_clients_mu || !s_frame_mu || !s_frame_sem) return ESP_ERR_NO_MEM;

    BaseType_t ok = xTaskCreate(http_start_task, "a_http_start", 4096, NULL, 5, NULL);
    if (ok != pdPASS) return ESP_ERR_NO_MEM;
    // Pin broadcast task to core 0 with Wi-Fi/httpd to keep DSP (core 1) clear of jitter.
    ok = xTaskCreatePinnedToCore(broadcast_task, "a_http_bcast", 3072, NULL, 4, NULL, 0);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t a_http_stop(void) {
    if (s_heartbeat_timer) {
        esp_timer_stop(s_heartbeat_timer);
        esp_timer_delete(s_heartbeat_timer);
        s_heartbeat_timer = NULL;
    }
    if (!s_server) return ESP_OK;
    esp_err_t ok = httpd_stop(s_server);
    s_server = NULL;
    return ok;
}
