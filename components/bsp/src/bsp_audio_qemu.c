// QEMU backend for the board-level audio API.
//
// Espressif's ESP32-C3 QEMU machine has no I2S or ES8311 device, so it cannot
// render speaker output or capture a physical microphone. This backend keeps
// firmware audio flows executable by modelling an accepted PCM stream and a
// silent microphone input. The physical ES8311 implementation remains in
// bsp_audio.c for device builds.
#include "bsp_audio.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "bsp_audio";
static bool s_initialized;
static uint32_t s_sample_rate;
static uint8_t s_bits;
static uint8_t s_channels;
static uint8_t s_volume;
static size_t s_written_bytes;

esp_err_t bsp_audio_init(void)
{
    s_initialized = true;
    s_volume = 100;
    ESP_LOGI(TAG, "QEMU virtual audio ready (PCM stream; no host speaker or microphone)");
    return ESP_OK;
}

esp_err_t bsp_audio_set_format(uint32_t hz, uint8_t bits, uint8_t ch)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (hz == 0 || bits != 16 || (ch != 1 && ch != 2)) return ESP_ERR_NOT_SUPPORTED;

    s_sample_rate = hz;
    s_bits = bits;
    s_channels = ch;
    ESP_LOGI(TAG, "QEMU virtual audio format %luHz/%ubit/%uch",
             (unsigned long)s_sample_rate, s_bits, s_channels);
    return ESP_OK;
}

esp_err_t bsp_audio_write(const void *pcm, size_t bytes)
{
    if (!s_initialized || !s_sample_rate || !pcm) return ESP_ERR_INVALID_STATE;
    s_written_bytes += bytes;
    vTaskDelay(pdMS_TO_TICKS(1));
    return ESP_OK;
}

esp_err_t bsp_audio_read(void *pcm, size_t bytes)
{
    if (!s_initialized || !s_sample_rate || !pcm) return ESP_ERR_INVALID_STATE;
    memset(pcm, 0, bytes);
    vTaskDelay(pdMS_TO_TICKS(1));
    return ESP_OK;
}

void bsp_audio_set_volume(uint8_t percent)
{
    s_volume = percent > 100 ? 100 : percent;
}
