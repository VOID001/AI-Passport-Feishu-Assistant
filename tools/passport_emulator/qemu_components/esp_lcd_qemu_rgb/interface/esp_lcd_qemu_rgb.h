#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"

typedef enum {
    RGB_QEMU_BPP_32 = 32,
    RGB_QEMU_BPP_16 = 16,
} esp_lcd_rgb_qemu_bpp_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    esp_lcd_rgb_qemu_bpp_t bpp;
} esp_lcd_rgb_qemu_config_t;

esp_err_t esp_lcd_new_rgb_qemu(const esp_lcd_rgb_qemu_config_t *config,
                               esp_lcd_panel_handle_t *panel);
