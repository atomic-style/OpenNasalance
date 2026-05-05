

#include "init.h"
#include "init_display.h"
#include "init_touch.h"
#include "a_evt.h"
#include "a_evt_user.h"
#include "atomic_bits.h"
#include "atomic_nvs.h"
#include "atomic_err.h"
#include "atomic_log.h"
#include "config.h"
#include "esp_err.h"
#include "a_mic.h"
#include "nasometer.h"
#ifdef ENABLE_BME280
#include "pressure.h"
#endif

#ifdef ENABLE_SD
#include "atomic_sd.h"
#endif

static const char *TAG = "、";

static esp_err_t init_nvs_defaults(void) {
    uint8_t device_id = (uint8_t)DEV_ID;
    char device_chip_id[16] = {'\0'};
    char device_unit_name[16] = {'\0'};
    strcpy(device_chip_id, DEV_CHIP_ID);
    strcpy(device_unit_name, DEV_UNIT);
    try(a_nvs_set_u8("device_id", device_id));
    try(a_nvs_set_str("chip_id", device_chip_id));
    try(a_nvs_set_str("unit_name", device_unit_name));
    return ESP_OK;
}

static esp_err_t init_nvs(void) {
    try(a_nvs_init());
    nvs_type_t device_id_type;
    esp_err_t ok = a_nvs_find_key("device_id", &device_id_type);
    if (ok != ESP_OK)
        try(init_nvs_defaults());
    return ESP_OK;
}

#ifdef ENABLE_SD
static esp_err_t init_sd(void) {
#ifdef SDMMC
    a_sdmmc_cfg_t cfg = {
        .width = SDMMC_SLOT_WIDTH,
        .clk_io_num = PIN_SDMMC_CLK,
        .cmd_io_num = PIN_SDMMC_CMD,
        .data_io_num = PIN_SDMMC_DATA0,
        .data_1_io_num = PIN_SDMMC_DATA1,
        .data_2_io_num = PIN_SDMMC_DATA2,
        .data_3_io_num = PIN_SDMMC_DATA3,
    };
    return atomic_sdmmc_init(&cfg);
#elif defined(SDSPI)
    a_sdspi_cfg_t cfg = {
        .host = SDSPI_HOST,
        .mosi_io_num = PIN_SDSPI_MOSI,
        .miso_io_num = PIN_SDSPI_MISO,
        .sclk_io_num = PIN_SDSPI_SCLK,
        .cs_io_num = PIN_SDSPI_CS,
    };
    return atomic_sdspi_init(&cfg);
#else
    warn(TAG, "ENABLE_SD set but no SDMMC/SDSPI pin block for this board");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
#endif // ENABLE_SD

void init() {
    notice(TAG, "init()");
    try(init_nvs());
    try(a_bits_init());
    try(a_evt_init());

#ifdef ENABLE_WIFI
    // try(init_net());
#endif // ENABLE_WIFI

#ifdef ENABLE_SD
    // Mount SD before display so atomic_lvgl's task picks up BIT_SD_READY
    // before registering its filesystem driver.
    if (init_sd() != ESP_OK) {
        warn(TAG, "SD mount failed — continuing without storage");
    }
#endif // ENABLE_SD

#ifdef ENABLE_DISPLAY
    try(init_display());
#endif // ENABLE_DISPLAY

#ifdef ENABLE_TOUCH
    try(init_touch());
#endif // ENABLE_TOUCH

    try(a_evt_user_init());

#ifdef ENABLE_BME280
    // Pressure-only mode: skip mic init and replace the nasometer UI with a
    // rolling BME280 trace. See ADDITION.md.
    try(pressure_init());
#elif defined(ENABLE_MIC)

    a_mic_441_config_t a_mic_441_config = {
        .pin_clk = PIN_MIC_CLK,
        .pin_ws = PIN_MIC_WS,
        .pin_data_1 = PIN_MIC_DATA_1,
        .pin_data_2 = PIN_MIC_DATA_2,
        .sample_rate = MIC_SAMPLE_RATE,
    };
    try(a_mic_441_init(&a_mic_441_config));

#ifdef ENABLE_LVGL
    try(nasometer_init());
#endif // ENABLE_LVGL

    /*
    a_mic_t5837_config_t t5837_cfg = {
        .pin_clk = PIN_MIC_CLK,
        .pin_data = PIN_MIC_DATA,
        .sample_rate = MIC_SAMPLE_RATE,
        .buf_samples = MIC_BUF_SAMPLES,
    };
    try(a_mic_t5837_init(&t5837_cfg));
    */

#endif // ENABLE_BME280 / ENABLE_MIC

    info(TAG, "init() done.");
}