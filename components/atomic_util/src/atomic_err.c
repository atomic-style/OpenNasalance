#include "esp_err.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "atomic_err.h"

static const char *col_reset = "\033[0m";
static const char *col_r = "\033[38;5;196m";
/*
static const char *col_o = "\033[38;5;202m";
static const char *col_y = "\033[38;5;220m";
static const char *col_g = "\033[38;5;46m";
static const char *col_b = "\033[38;5;21m";
static const char *col_v = "\033[38;5;93m";
static const char *col_p = "\033[38;5;200m";
static const char *col_c = "\033[38;5;51m";
static const char *col_w = "\033[38;5;255m";
*/

static void atomic_fail_print(const char *msg, esp_err_t rc, const char *file, int line, const char *function, const char *expression, intptr_t addr)
{
    esp_rom_printf("%s%s %s failed: esp_err_t 0x%x", col_r, "ア", msg, rc);
    esp_rom_printf(" (%s)", esp_err_to_name(rc));
    esp_rom_printf(" at 0x%08x%s\n", esp_cpu_get_call_addr(addr), col_reset);
// #if !CONFIG_APP_BUILD_TYPE_PURE_RAM_APP
//     if (spi_flash_cache_enabled())
// #endif
//     {
        esp_rom_printf("%s󰘍 file: \"%s\" line %d\nfunc: %s\nexpression: %s%s\n", col_r, file, line, function, expression, col_reset);
//     }
}

void _atomic_fail(esp_err_t rc, const char *file, int line, const char *function, const char *expression)
{
    atomic_fail_print("ESP_ERROR_CHECK", rc, file, line, function, expression, (intptr_t)__builtin_return_address(0));
    abort();
}