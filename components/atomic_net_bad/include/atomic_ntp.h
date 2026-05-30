#pragma once

#include "atomic_net.h"
#include "esp_err.h"
#include "esp_sntp.h"

typedef struct a_ntp_cfg_s {
    bool enable;
    char uri[32];
    char tz[16];
} a_ntp_cfg_t;

uint64_t a_ntp_ms(void);
void atomic_ntp_hms(char *buf, size_t buflen);

time_t atomic_time(void);
uint64_t atomic_ntp_ms(void);
char *atomic_ntp_ts(void);
void atomic_ntp_print(void);

void a_ntp_cb(struct timeval *tv);
void a_ntp_init(void);
esp_err_t a_ntp_config(a_ntp_cfg_t *ntp_config);
