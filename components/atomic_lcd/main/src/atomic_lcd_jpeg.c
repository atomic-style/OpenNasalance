#include "atomic_lcd_jpeg.h"

#include "atomic_err.h"
#include "atomic_lcd.h"
#include "atomic_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_dec.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "atomic_jpeg";

#define JPEG_DECODE_LINES 64

static jpeg_pixel_format_t j_type = JPEG_PIXEL_FORMAT_RGB565_BE;
static jpeg_rotate_t j_rotation = JPEG_ROTATE_0D;

esp_err_t a_jpeg_14(const char *filename) {
    ESP_LOGI(TAG, "a_jpeg_info(%s)", filename);
    a_lcd_t *a_lcd = a_lcd_get();
    if (!a_lcd) {
        warn(TAG, "a_jpeg_14() failed to get lcd");
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t h_res = a_lcd->cfg->h_res;
    uint32_t v_res = a_lcd->cfg->v_res;
    uint32_t max_transfer_size = a_lcd->cfg->max_transfer_sz;
    uint32_t max_transfer_lines = max_transfer_size / (h_res * 2);
    uint32_t out_buffer_size = h_res * 2 * max_transfer_lines;
    uint32_t out_data_bytes = h_res * 2 * v_res;
    info(TAG,
         "lcd h_res: %d, v_res: %d, max_transfer_size: %d, max_transfer_lines: %d, out_buffer_size: %d, "
         "out_data_bytes: %d",
         h_res, v_res, max_transfer_size, max_transfer_lines, out_buffer_size, out_data_bytes);

    FILE *file = fopen(filename, "rb");
    fseek(file, 0, SEEK_END);
    int file_length = ftell(file);
    fseek(file, 0, SEEK_SET);
    info(TAG, "allocate input buffer size: %d", file_length);
    uint8_t *input_buffer = (uint8_t *)malloc(file_length);
    if (!input_buffer) {
        warn(TAG, "input buffer allocation failed");
        return JPEG_ERR_NO_MEM;
    }
    fread(input_buffer, 1, file_length, file);
    fclose(file);

    jpeg_dec_io_t *jpeg_io;
    jpeg_dec_header_info_t *jpeg_header_info;
    jpeg_dec_handle_t jpeg_dec;

    jpeg_dec_config_t jpeg_dec_config = DEFAULT_JPEG_DEC_CONFIG();
    jpeg_dec_config.output_type = j_type;
    jpeg_dec_config.rotate = j_rotation;
    jpeg_dec_config.block_enable = true;
    jpeg_error_t jerr = jpeg_dec_open(&jpeg_dec_config, &jpeg_dec);
    if (jerr != JPEG_ERR_OK) {
        warn(TAG, "jpeg_dec_open() error %d", jerr);
        return jerr;
    }

    jpeg_io = calloc(1, sizeof(jpeg_dec_io_t));
    jpeg_header_info = calloc(1, sizeof(jpeg_dec_header_info_t));

    jpeg_io->inbuf = input_buffer;
    jpeg_io->inbuf_len = file_length;

    jerr = jpeg_dec_parse_header(jpeg_dec, jpeg_io, jpeg_header_info);
    if (jerr != JPEG_ERR_OK) {
        warn(TAG, "jpeg_dec_parse_header() error %d", jerr);
        return jerr;
    }
    uint32_t jpeg_width = jpeg_header_info->width;
    uint32_t jpeg_height = jpeg_header_info->height;
    uint32_t jpeg_bytes = jpeg_width * jpeg_height * 2;
    debug(TAG, "jpeg_width: %d, jpeg_height: %d, jpeg_bytes: %d", jpeg_width, jpeg_height, jpeg_bytes);

    uint8_t *output_buffer = jpeg_calloc_align(out_buffer_size, 16);
    if (!output_buffer) {
        warn(TAG, "output_buffer allocation failed");
        jpeg_dec_close(jpeg_dec);
        return JPEG_ERR_NO_MEM;
    }
    info(TAG, "Allocated output_buffer, size is %d", out_buffer_size);

    jpeg_io->outbuf = output_buffer;
    jpeg_io->out_size = out_buffer_size;

    uint32_t data_written = 0;
    while (data_written < jpeg_bytes) {
        jerr = jpeg_dec_process(jpeg_dec, jpeg_io);
        if (jerr != JPEG_ERR_OK) {
            warn(TAG, "jpeg_dec_process() error %d", jerr);
            return jerr;
        }
        int lines_decoded = jpeg_io->out_size / (jpeg_width * 2);
        esp_lcd_panel_draw_bitmap(a_lcd->panel, 0, data_written / (jpeg_width * 2), jpeg_width,
                                  (data_written / (jpeg_width * 2)) + lines_decoded, output_buffer);
        xSemaphoreTake(a_lcd->sem, portMAX_DELAY);
        data_written += jpeg_io->out_size;
    }

    debug(TAG, "JPEG decode complete.");

    jpeg_dec_close(jpeg_dec);
    free(jpeg_io);
    free(output_buffer);
    free(input_buffer);
    return ESP_OK;
}
