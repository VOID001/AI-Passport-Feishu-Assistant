#include <assert.h>
#include <stdlib.h>

#include "esp_check.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_qemu_rgb.h"
#include "soc/syscon_reg.h"

#define QEMU_ORIGIN 0x51454d55U

typedef volatile struct {
    uint32_t version;
    struct { uint32_t height: 16; uint32_t width: 16; } size;
    struct { uint32_t y: 16; uint32_t x: 16; } update_from;
    struct { uint32_t y: 16; uint32_t x: 16; } update_to;
    void *update_content;
    struct { uint32_t ena: 1; uint32_t reserved: 31; } update_status;
    uint32_t bpp;
} qemu_rgb_device_t;

typedef struct {
    esp_lcd_panel_t base;
    uint32_t width;
    uint32_t height;
} qemu_rgb_panel_t;

static qemu_rgb_device_t *const device = (void *)0x21000000;

// QEMU has no ADC conversion-complete event. This QEMU-only replacement keeps
// ESP-IDF's Wi-Fi archive from retaining its pre-app ADC2 calibration routine.
void adc2_cal_include(void) {}

static esp_err_t panel_delete(esp_lcd_panel_t *panel) { free(panel); return ESP_OK; }
static esp_err_t panel_noop(esp_lcd_panel_t *panel) { (void)panel; return ESP_OK; }
static esp_err_t panel_bool(esp_lcd_panel_t *panel, bool value) { (void)panel; (void)value; return ESP_OK; }
static esp_err_t panel_mirror(esp_lcd_panel_t *panel, bool x, bool y) { (void)panel; (void)x; (void)y; return ESP_OK; }
static esp_err_t panel_gap(esp_lcd_panel_t *panel, int x, int y) { (void)panel; (void)x; (void)y; return ESP_OK; }

static esp_err_t panel_draw(esp_lcd_panel_t *panel, int x_start, int y_start,
                            int x_end, int y_end, const void *pixels) {
    (void)panel;
    assert(x_start < x_end && y_start < y_end);
    device->update_from.x = x_start;
    device->update_from.y = y_start;
    device->update_to.x = x_end;
    device->update_to.y = y_end;
    device->update_content = (void *)pixels;
    device->update_status.ena = 1;
    while (device->update_status.ena) {}
    return ESP_OK;
}

esp_err_t esp_lcd_new_rgb_qemu(const esp_lcd_rgb_qemu_config_t *config,
                               esp_lcd_panel_handle_t *ret_panel) {
    ESP_RETURN_ON_FALSE(config && ret_panel, ESP_ERR_INVALID_ARG, "qemu_rgb", "invalid panel configuration");
    ESP_RETURN_ON_FALSE(REG_READ(SYSCON_DATE_REG - 4) == QEMU_ORIGIN,
                        ESP_ERR_NOT_SUPPORTED, "qemu_rgb", "virtual panel requires QEMU");
    qemu_rgb_panel_t *panel = calloc(1, sizeof(*panel));
    ESP_RETURN_ON_FALSE(panel, ESP_ERR_NO_MEM, "qemu_rgb", "panel allocation failed");
    panel->width = config->width;
    panel->height = config->height;
    device->size.width = config->width;
    device->size.height = config->height;
    device->bpp = config->bpp;
    panel->base.del = panel_delete;
    panel->base.reset = panel_noop;
    panel->base.init = panel_noop;
    panel->base.draw_bitmap = panel_draw;
    panel->base.invert_color = panel_bool;
    panel->base.mirror = panel_mirror;
    panel->base.swap_xy = panel_bool;
    panel->base.set_gap = panel_gap;
    panel->base.disp_on_off = panel_bool;
    *ret_panel = &panel->base;
    return ESP_OK;
}
