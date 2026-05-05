// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include <stdio.h>
#include <stdarg.h>
#include "esp_log.h"
#include "atomic_log.h"

static const char *char_info = "め";
static const char *char_console = "ば";
static const char *char_debug = "〡";
static const char *char_notice = "〳";
static const char *char_alert = "も";
static const char *char_warn = "と";
static const char *char_err = "だ";

static const char *col_reset = "\033[0m";

// foreground
static const char *col_bk = "\033[38;5;0m";
static const char *col_r = "\033[38;5;196m";
static const char *col_y = "\033[38;5;220m";
static const char *col_p = "\033[38;5;200m";
static const char *col_c = "\033[38;5;51m";
static const char *col_g = "\033[38;5;46m";
static const char *col_b = "\033[38;5;21m";
static const char *col_v = "\033[38;5;93m";
/*
static const char *col_o = "\033[38;5;202m";
static const char *col_w = "\033[38;5;255m";
*/

// background
static const char *col_bg_r = "\033[48;5;196m";
static const char *col_bg_y = "\033[48;5;220m";

// debug    [〡 䋧]   fg=violet
// console  [ば 󰜈]   fg=blue
// info     [め 䌇]   fg=green
// notice   [〳 䈂]   fg=cyan
// alert    [も 󰎈]   fg=pink
// warn     と 䈂   fg=black, bg=yellow
// err      ど ㇑   fg=yellow, bg=red

void atomic_log_print_debug(const char *tag, const char *format, ...)
{
    char msg[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    printf("%s[%s%s] %s%s\n", col_v, char_debug, tag, msg, col_reset);
}

void atomic_log_print_console(const char *tag, const char *format, ...)
{
    char msg[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    printf("%s[%s%s] %s%s\n", col_b, char_console, tag, msg, col_reset);
}

void atomic_log_print_info(const char *tag, const char *format, ...)
{
    char msg[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    printf("%s[%s%s] %s%s\n", col_g, char_info, tag, msg, col_reset);
}

void atomic_log_print_notice(const char *tag, const char *format, ...)
{
    char msg[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    printf("%s[%s%s] %s%s\n", col_c, char_notice, tag, msg, col_reset);
}

void atomic_log_print_alert(const char *tag, const char *format, ...)
{
    char msg[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    printf("%s[%s%s] %s%s\n", col_p, char_alert, tag, msg, col_reset);
}

void atomic_log_print_warn(const char *tag, const char *format, ...)
{
    char msg[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    printf("%s[%s%s] %s%s\n", col_y, char_warn, tag, msg, col_reset);
}

void atomic_log_print_err(const char *tag, const char *format, ...)
{
    char msg[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    printf("%s[%s%s] %s%s\n", col_r, char_err, tag, msg, col_reset);
}