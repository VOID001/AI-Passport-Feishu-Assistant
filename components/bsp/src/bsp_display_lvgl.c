// components/bsp/src/bsp_display_lvgl.c
// LVGL 接入单独成文件:不用 LVGL 的开发者删掉本文件 + idf_component.yml 里的两条依赖即可。
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

#ifndef CONFIG_AI_PASSPORT_QEMU
#define CONFIG_AI_PASSPORT_QEMU 0
#endif

#if CONFIG_AI_PASSPORT_QEMU
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#endif

static const char *TAG = "bsp_lvgl";

static lv_display_t *s_disp;
#if CONFIG_AI_PASSPORT_QEMU
static void *s_qemu_draw_buffer;

static void qemu_flush(lv_display_t *display, const lv_area_t *area, uint8_t *color_map) {
    esp_lcd_panel_draw_bitmap(bsp_display_panel(),
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              color_map);
    lv_display_flush_ready(display);
}
#endif

lv_display_t *bsp_lvgl_init(void) {
    if (s_disp) return s_disp;
    if (!bsp_display_panel()) {
        ESP_LOGE(TAG, "请先成功调用 bsp_display_init()");
        return NULL;
    }

    const lvgl_port_cfg_t pc = ESP_LVGL_PORT_INIT_CONFIG();
    if (lvgl_port_init(&pc) != ESP_OK) {
        ESP_LOGE(TAG, "lvgl_port_init 失败");
        return NULL;
    }

#if CONFIG_AI_PASSPORT_QEMU
    const size_t buffer_size = (size_t)BSP_LCD_W * 20 * sizeof(lv_color_t);
    s_qemu_draw_buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_qemu_draw_buffer) {
        ESP_LOGE(TAG, "QEMU LVGL 缓冲分配失败");
        return NULL;
    }
    s_disp = lv_display_create(BSP_LCD_W, BSP_LCD_H);
    if (!s_disp) {
        ESP_LOGE(TAG, "QEMU LVGL display 创建失败");
        return NULL;
    }
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_disp, s_qemu_draw_buffer, NULL, buffer_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, qemu_flush);
    ESP_LOGI(TAG, "QEMU LVGL 直接刷新就绪");
    return s_disp;
#else
    const lvgl_port_display_cfg_t dc = {
        .panel_handle = bsp_display_panel(),
        .io_handle    = bsp_display_io(),
        // ⚠ C3 无 PSRAM,DMA 只能用内部 RAM(总共约 150KB)。
        // 20 行单缓冲 ≈ 9.6KB;若改成 40 行双缓冲(≈37.5KB)会把 I2S 等外设的
        // DMA 描述符挤到 NO_MEM。刷新略慢但稳。
        .buffer_size   = (uint32_t)BSP_LCD_W * 20,
        .double_buffer = false,
        .hres = BSP_LCD_W, .vres = BSP_LCD_H,
        // 旋转/镜像必须在这里配:esp_lvgl_port 注册显示时会重新下发 MADCTL,
        // 覆盖 bsp_display.c 里 esp_lcd_panel_mirror() 的设置。
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        // swap_bytes:LVGL 输出小端 RGB565,ST7789 走 SPI 要大端 → 需交换高低字节。
        .flags = {
            .buff_dma = !CONFIG_AI_PASSPORT_QEMU,
            .swap_bytes = !CONFIG_AI_PASSPORT_QEMU,
        },
    };
    s_disp = lvgl_port_add_disp(&dc);
    if (!s_disp) { ESP_LOGE(TAG, "lvgl_port_add_disp 失败"); return NULL; }

    ESP_LOGI(TAG, "LVGL 就绪");
    return s_disp;
#endif
}

bool bsp_lvgl_lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_lvgl_unlock(void)         { lvgl_port_unlock(); }
