#include "atomic_wav.h"
#include "esp_err.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "atomic_wav";

static int read_exact(FILE *fp, void *buf, size_t n) { return fread(buf, 1, n, fp) == n ? 0 : -1; }

static esp_err_t atomic_parse_format_chunk(FILE *file, wav_info_t *wav_info, uint32_t *read_loc) {
    uint32_t header_size;
    read_exact(file, &header_size, 4);
    read_exact(file, &wav_info->compression_code, 2);
    read_exact(file, &wav_info->num_channels, 2);
    read_exact(file, &wav_info->sample_rate, 4);
    read_exact(file, &wav_info->byte_rate, 4);
    read_exact(file, &wav_info->block_align, 2);
    read_exact(file, &wav_info->bits_per_sample, 2);
    *read_loc += 20;

    if (header_size > 16) {
        uint16_t extra_data_size;
        read_exact(file, &extra_data_size, 2);
        fseek(file, extra_data_size, SEEK_CUR);
        *read_loc += (2 + extra_data_size);
    }

    if (header_size % 2 != 0) {
        fseek(file, 1, SEEK_CUR);
        *read_loc += 1;
    }

    ESP_LOGI(TAG, "format: comp=%u ch=%u rate=%u br=%u align=%u bits=%u", (unsigned)wav_info->compression_code,
             (unsigned)wav_info->num_channels, (unsigned)wav_info->sample_rate, (unsigned)wav_info->byte_rate,
             (unsigned)wav_info->block_align, (unsigned)wav_info->bits_per_sample);
    return ESP_OK;
}

static esp_err_t atomic_wav_info(FILE *file, wav_info_t *wav_info) {
    uint32_t read_loc = 0;
    fseek(file, 0, SEEK_END);
    uint32_t wav_file_size = ftell(file);

    fseek(file, 0, SEEK_SET);
    char riff[5] = {0};
    read_exact(file, riff, 4);
    read_loc += 4;
    if (memcmp(riff, "RIFF", 4) != 0) {
        ESP_LOGE(TAG, "Not a valid WAV file: %s", riff);
        return ESP_FAIL;
    }

    uint32_t header_file_size;
    read_exact(file, &header_file_size, 4);
    read_loc += 4;
    header_file_size += 8;
    if (header_file_size != wav_file_size) {
        ESP_LOGW(TAG, "Header file size does not match actual file size");
    }
    wav_info->size = header_file_size;

    char wave[5] = {0};
    read_exact(file, wave, 4);
    read_loc += 4;
    if (memcmp(wave, "WAVE", 4) != 0) {
        ESP_LOGE(TAG, "Not a valid WAV file: %s", wave);
        return ESP_FAIL;
    }

    while (read_loc < header_file_size) {
        char chunk_id[5] = {0};
        read_exact(file, chunk_id, 4);
        read_loc += 4;
        if (memcmp(chunk_id, "data", 4) == 0) {
            read_exact(file, &wav_info->data_size, 4);
            wav_info->data_location = read_loc;
            read_loc += wav_info->data_size;
            return ESP_OK;
        } else if (memcmp(chunk_id, "fmt ", 4) == 0) {
            ESP_ERROR_CHECK(atomic_parse_format_chunk(file, wav_info, &read_loc));
        } else {
            uint32_t junk_size;
            read_exact(file, &junk_size, 4);
            read_loc += 4;
            if (junk_size % 2 != 0)
                junk_size++;
            // fseek(file, junk_size, SEEK_CUR);
            char junk[junk_size + 1];
            junk[junk_size] = '\0';
            read_exact(file, junk, junk_size);
            ESP_LOGI(TAG, "DEBUG: chunk id: %s, size: %u, data: %s", chunk_id, (unsigned)junk_size, junk);
            read_loc += junk_size;
        }
    }
    return ESP_FAIL;
}

esp_err_t atomic_wav_play(const char *filepath) {
    if (!filepath || filepath[0] == '\0') {
        ESP_LOGE(TAG, "Invalid filepath");
        return ESP_ERR_INVALID_ARG;
    }

    FILE *file = fopen(filepath, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        return ESP_ERR_NOT_FOUND;
    }

    wav_info_t wav_info = {0};
    esp_err_t ret = atomic_wav_info(file, &wav_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse WAV info");
        fclose(file);
        return ret;
    }

    // Validate WAV format
    if (wav_info.compression_code != 1) { // 1 = PCM
        ESP_LOGE(TAG, "Unsupported WAV compression code: %u", wav_info.compression_code);
        fclose(file);
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (wav_info.bits_per_sample != 16) {
        ESP_LOGE(TAG, "Unsupported bits per sample: %u (only 16-bit supported)", wav_info.bits_per_sample);
        fclose(file);
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (wav_info.num_channels != 1 && wav_info.num_channels != 2) {
        ESP_LOGE(TAG, "Unsupported channel count: %u (only mono/stereo supported)", wav_info.num_channels);
        fclose(file);
        return ESP_ERR_NOT_SUPPORTED;
    }

    atomic_audio_config_t audio_config = {
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .slot_mode = (wav_info.num_channels == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO,
        .sample_rate_hz = wav_info.sample_rate,
    };

    ret = atomic_i2s_configure(&audio_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S configure failed: %s", esp_err_to_name(ret));
        fclose(file);
        return ret;
    }

    if (fseek(file, wav_info.data_location, SEEK_SET) != 0) {
        ESP_LOGE(TAG, "Failed to seek to data location");
        fclose(file);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ret = atomic_i2s_write_file(file, wav_info.data_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(ret));
        fclose(file);
        return ret;
    }

    fclose(file);
    return ESP_OK;
}