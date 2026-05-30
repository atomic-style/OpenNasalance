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
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "HTTP";

#define MAX_WS_CLIENTS 4
#define FRAME_HEADER 8 // type, ver, frame_idx_lo, frame_idx_hi, n_bins, amp_top, amp_bot, nasalance
#define FRAME_MAX_BYTES (FRAME_HEADER + 2 * A_HTTP_MAX_BINS)

static httpd_handle_t s_server = NULL;
static esp_timer_handle_t s_heartbeat_timer = NULL;
static const char *s_unit_name = "unknown";

// IDF's WS dispatcher (httpd_uri.c:362) explicitly does NOT call our handler
// for the initial GET — it sends the 101 and stashes our handler for future
// inbound frames only. So we can't register clients from the handler. Instead
// we discover live WS sockets on every broadcast via httpd_get_client_list +
// httpd_ws_get_fd_info. s_hello_sent tracks which fds we've already greeted
// so each new client gets exactly one hello, regardless of which tick first
// observes it. Stale fds are pruned each heartbeat.
static int s_hello_sent[MAX_WS_CLIENTS];
static volatile bool s_has_ws_clients = false; // last-known, updated by heartbeat

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
// Live WS client discovery (replaces the old hand-rolled client list).
//
// httpd_get_client_list returns every fd the server currently knows about
// (both plain HTTP and WS); httpd_ws_get_fd_info filters those down to the
// ones in the WebSocket state. This is the only reliable way to enumerate
// WS clients because IDF's WS dispatcher never calls our handler for the
// initial handshake (see comment block on s_hello_sent above).
// ---------------------------------------------------------------------------

// Fill `out` with at most `max` live WS-state fds. Returns the count.
static int ws_clients(int *out, int max) {
    if (!s_server) return 0;
    int all[MAX_WS_CLIENTS * 2 + 4]; // small overhead in case non-WS HTTP fds exist
    size_t n = sizeof all / sizeof all[0];
    if (httpd_get_client_list(s_server, &n, all) != ESP_OK) return 0;
    int k = 0;
    for (size_t i = 0; i < n && k < max; i++) {
        if (httpd_ws_get_fd_info(s_server, all[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            out[k++] = all[i];
        }
    }
    return k;
}

static bool hello_already_sent(int fd) {
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_hello_sent[i] == fd) return true;
    }
    return false;
}

static void hello_mark_sent(int fd) {
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_hello_sent[i] == 0) {
            s_hello_sent[i] = fd;
            return;
        }
    }
}

// Drop hello-set entries that are no longer in the live list (the fd was
// closed). Keeps s_hello_sent from filling up over time.
static void hello_prune(const int *live, int n_live) {
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_hello_sent[i] == 0) continue;
        bool still = false;
        for (int j = 0; j < n_live; j++) {
            if (live[j] == s_hello_sent[i]) {
                still = true;
                break;
            }
        }
        if (!still) s_hello_sent[i] = 0;
    }
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
// File access — list and download recordings from the SD mount (/sd)
// ---------------------------------------------------------------------------

#define SD_MOUNT "/sd"

// GET /files — minimal HTML index of files on the SD card with download links.
static esp_err_t files_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(
        req, "<!doctype html><meta name=viewport content=\"width=device-width\">"
             "<title>Recordings</title><h2>Recordings</h2><ul>");
    DIR *d = opendir(SD_MOUNT);
    if (!d) {
        httpd_resp_sendstr_chunk(req, "<li><em>SD card not mounted</em></li>");
    } else {
        struct dirent *de;
        char line[320];
        int n = 0;
        while ((de = readdir(d)) != NULL) {
            if (de->d_type != DT_REG) continue;
            // Bound each %s: d_name can be up to 255 bytes, but our recordings
            // are short. Precision keeps the line within the buffer.
            snprintf(line, sizeof(line), "<li><a href=\"/download?f=%.120s\">%.120s</a></li>",
                     de->d_name, de->d_name);
            httpd_resp_sendstr_chunk(req, line);
            n++;
        }
        closedir(d);
        if (n == 0) httpd_resp_sendstr_chunk(req, "<li><em>(no files yet)</em></li>");
    }
    httpd_resp_sendstr_chunk(req, "</ul>");
    httpd_resp_sendstr_chunk(req, NULL); // end response
    return ESP_OK;
}

static const httpd_uri_t s_files_uri = {
    .uri = "/files",
    .method = HTTP_GET,
    .handler = files_handler,
    .user_ctx = NULL,
};

// GET /download?f=<name> — stream a single file from /sd as an attachment.
static esp_err_t download_handler(httpd_req_t *req) {
    char query[128];
    char name[80];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "f", name, sizeof(name)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing 'f' parameter");
        return ESP_FAIL;
    }
    // Reject path traversal — only a bare filename within /sd is allowed.
    if (name[0] == '\0' || strchr(name, '/') || strstr(name, "..")) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid filename");
        return ESP_FAIL;
    }

    char path[96];
    snprintf(path, sizeof(path), "%s/%s", SD_MOUNT, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "file not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "audio/wav");
    char cd[128];
    snprintf(cd, sizeof(cd), "attachment; filename=\"%s\"", name);
    httpd_resp_set_hdr(req, "Content-Disposition", cd);

    char *buf = malloc(2048);
    if (!buf) {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
        return ESP_FAIL;
    }
    size_t n;
    esp_err_t ret = ESP_OK;
    while ((n = fread(buf, 1, 2048, f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            warn(TAG, "download: client aborted %s", name);
            ret = ESP_FAIL;
            break;
        }
    }
    free(buf);
    fclose(f);
    if (ret == ESP_OK) httpd_resp_send_chunk(req, NULL, 0); // end response
    return ret;
}

static const httpd_uri_t s_download_uri = {
    .uri = "/download",
    .method = HTTP_GET,
    .handler = download_handler,
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

// IDF's dispatcher never calls this handler for the initial GET — it sends
// the 101 itself and stashes a pointer to us for *inbound* frames only.
// So the handshake / hello logic lives in broadcast_heartbeat instead; this
// just drains anything the client decides to send us (currently nothing).
static esp_err_t ws_handler(httpd_req_t *req) {
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
    // Fast-path skip if no WS clients connected. s_has_ws_clients is updated
    // by the 1 Hz heartbeat tick, so there's up to 1 s of lag after a new
    // client appears before binary frames start flowing — acceptable here.
    if (!s_has_ws_clients) return;

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
    int n = ws_clients(fds, MAX_WS_CLIENTS);
    for (int i = 0; i < n; i++) {
        esp_err_t ok = ws_send_binary(fds[i], local, local_len);
        if (ok != ESP_OK) {
            warn(TAG, "ws_send_binary fd=%d FAIL: %s", fds[i], esp_err_to_name(ok));
            // No client-list to remove from; the next heartbeat will observe
            // the closed fd and update s_has_ws_clients / hello_prune.
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
    int n = ws_clients(fds, MAX_WS_CLIENTS);
    s_has_ws_clients = (n > 0);
    hello_prune(fds, n);
    if (n == 0) return;

    // Send hello once per new client. The handshake completes before our
    // handler ever runs (IDF dispatches it itself), so this tick is the
    // first place we can observe a freshly-connected fd.
    char hello[96];
    snprintf(hello, sizeof(hello), "{\"type\":\"hello\",\"unit\":\"%s\"}", s_unit_name);
    for (int i = 0; i < n; i++) {
        if (hello_already_sent(fds[i])) continue;
        if (ws_send_text(fds[i], hello) == ESP_OK) {
            hello_mark_sent(fds[i]);
            info(TAG, "ws hello → fd=%d", fds[i]);
        }
    }

    char msg[64];
    int64_t uptime_us = esp_timer_get_time();
    snprintf(msg, sizeof(msg), "{\"type\":\"heartbeat\",\"uptime\":%lld}", uptime_us / 1000000LL);
    for (int i = 0; i < n; i++) {
        esp_err_t ok = ws_send_text(fds[i], msg);
        if (ok != ESP_OK) {
            warn(TAG, "ws_send hb fd=%d FAIL: %s", fds[i], esp_err_to_name(ok));
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
    // Report whichever interface has an IP. In AP mode the STA netif exists
    // (atomic_wifi creates both up-front) but reads 0.0.0.0 — fall through to
    // the AP netif in that case.
    const char *keys[] = {"WIFI_STA_DEF", "WIFI_AP_DEF"};
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        esp_netif_t *nif = esp_netif_get_handle_from_ifkey(keys[i]);
        if (!nif) continue;
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(nif, &ip) != ESP_OK) continue;
        if (ip.ip.addr == 0) continue;
        info(TAG, "http server up at http://" IPSTR "/ (%s)", IP2STR(&ip.ip), keys[i]);
        return;
    }
    warn(TAG, "http server started but no interface has an IP yet");
}

static void http_start_task(void *arg) {
    // Start as soon as either interface is up: STA got a DHCP lease, OR
    // the SoftAP is broadcasting. The server binds to all netifs.
    a_bits_wait_any(BIT_WIFI_READY | BIT_WIFI_AP_READY);

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;
    // CONFIG_LWIP_MAX_SOCKETS is 12 in defaults (3 reserved by httpd internally).
    // Keep the cap conservative until we measure under real WS load.
    cfg.max_open_sockets = 7;
    // No custom close_fn: with the hand-rolled client list gone, the IDF
    // default (which just close()s the socket) is sufficient. Stale entries
    // in s_hello_sent are pruned on the next heartbeat tick.

    esp_err_t ok = httpd_start(&s_server, &cfg);
    if (ok != ESP_OK) {
        err(TAG, "httpd_start() FAIL: %s", esp_err_to_name(ok));
        s_server = NULL;
        vTaskDelete(NULL);
        return;
    }

    httpd_register_uri_handler(s_server, &s_root_uri);
    httpd_register_uri_handler(s_server, &s_ws_uri);
    httpd_register_uri_handler(s_server, &s_files_uri);
    httpd_register_uri_handler(s_server, &s_download_uri);

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
    s_frame_mu = xSemaphoreCreateMutex();
    s_frame_sem = xSemaphoreCreateBinary();
    if (!s_frame_mu || !s_frame_sem) return ESP_ERR_NO_MEM;

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
