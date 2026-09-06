// main/main.c —— FoloToy AI Passport BSP 驱动参考示例:初始化 + 菜单 + 按键分发。
//
// 按键语义(全局统一):
//   上/下 短按   菜单中=移动选中项;演示页中=该页自定义
//   确定  短按   菜单中=进入选中项;演示页中=该页自定义
//   确定  长按   演示页中=返回菜单(由本文件统一拦截)
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_pins.h"      // 错误日志里要打印 BSP_LCD_* 引脚号
#include "demo.h"
#include "ui_pixel.h"
#include "work_assistant.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_sleep.h"

#ifndef CONFIG_AI_PASSPORT_QEMU
#define CONFIG_AI_PASSPORT_QEMU 0
#endif

#if CONFIG_AI_PASSPORT_QEMU
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

static const char *TAG = "main";

#define SCREEN_WAKE_GUARD_MS    2500U

static const demo_entry_t DEMOS[] = {
    { "Display", demo_display_enter, demo_display_exit, demo_display_key },
    { "Button",  demo_button_enter,  demo_button_exit,  demo_button_key  },
    { "Audio",   demo_audio_enter,   demo_audio_exit,   demo_audio_key   },
    { "Battery", demo_battery_enter, demo_battery_exit, demo_battery_key },
    { "BLE",     demo_ble_enter,     demo_ble_exit,     demo_ble_key     },
    { "Low Power", demo_low_power_enter, demo_low_power_exit, demo_low_power_key },
};
#define DEMO_COUNT (sizeof(DEMOS) / sizeof(DEMOS[0]))

// 各外设初始化结果:失败的项在菜单里标 [FAIL] 且不允许进入。
static bool s_ok[DEMO_COUNT];

static lv_obj_t *s_menu_scr;
static lv_obj_t *s_cards[DEMO_COUNT];
static lv_obj_t *s_rows[DEMO_COUNT];
static lv_obj_t *s_mascot;
static int  s_sel;                 // 当前选中项
static int  s_active = -1;         // 当前所在演示页;-1 = 在菜单
static bool s_work_assistant_active;
static lv_timer_t *s_screen_idle_timer;
static uint32_t s_last_input_tick;
static uint32_t s_wake_guard_tick;
static bsp_btn_t s_wake_button;
static bool s_screen_awake = true;
static bool s_wake_guard;

#if CONFIG_AI_PASSPORT_QEMU
#define QEMU_DOUBLE_CLICK_MS 300U

static const char *qemu_button_name(bsp_btn_t button)
{
    return button == BSP_BTN_UP ? "UP" :
           button == BSP_BTN_DOWN ? "DOWN" : "OK";
}

static void qemu_emit_button(bsp_btn_t button, bsp_btn_ev_t event)
{
    bsp_button_inject(button, event);
    ESP_LOGI(TAG, "QEMU key: %s %s", qemu_button_name(button),
             event == BSP_BTN_PRESS ? "press" :
             event == BSP_BTN_CLICK ? "click" :
             event == BSP_BTN_DOUBLE ? "double" : "long");
}

static void qemu_flush_pending_click(bool *pending, bsp_btn_t *button,
                                     TickType_t *pressed_at)
{
    if (!*pending ||
        (xTaskGetTickCount() - *pressed_at) < pdMS_TO_TICKS(QEMU_DOUBLE_CLICK_MS)) {
        return;
    }
    qemu_emit_button(*button, BSP_BTN_CLICK);
    *pending = false;
}

static void qemu_handle_button(bsp_btn_t button, bool *pending,
                               bsp_btn_t *pending_button,
                               TickType_t *pressed_at)
{
    TickType_t now = xTaskGetTickCount();
    qemu_flush_pending_click(pending, pending_button, pressed_at);

    if (*pending && *pending_button == button) {
        qemu_emit_button(button, BSP_BTN_PRESS);
        qemu_emit_button(button, BSP_BTN_DOUBLE);
        *pending = false;
        return;
    }
    if (*pending) {
        qemu_emit_button(*pending_button, BSP_BTN_CLICK);
    }
    qemu_emit_button(button, BSP_BTN_PRESS);
    *pending = true;
    *pending_button = button;
    *pressed_at = now;
}

static bool qemu_decode_button(uint8_t key, uint8_t *escape_state,
                               bool *discard_lf, bsp_btn_t *button)
{
    if (*discard_lf) {
        *discard_lf = false;
        if (key == '\n') return false;
    }
    if (key == '\r') {
        *discard_lf = true;
        *button = BSP_BTN_OK;
        return true;
    }
    if (*escape_state == 0) {
        if (key == 0x1B) {
            *escape_state = 1;
            return false;
        }
        if (key == 'w') *button = BSP_BTN_UP;
        else if (key == 's') *button = BSP_BTN_DOWN;
        else if (key == 'e') *button = BSP_BTN_OK;
        else return false;
        return true;
    }
    if (*escape_state == 1) {
        *escape_state = key == '[' ? 2 : 0;
        return false;
    }
    if (key == 'A') *button = BSP_BTN_UP;
    else if (key == 'B') *button = BSP_BTN_DOWN;
    else {
        if (key < '@' || key > '~') return false;
        *escape_state = 0;
        return false;
    }
    *escape_state = 0;
    return true;
}

static void qemu_input_task(void *arg)
{
    (void)arg;
    uint8_t key;
    uint8_t escape_state = 0;
    bool discard_lf = false;
    bool pending = false;
    bsp_btn_t pending_button = BSP_BTN_UP;
    TickType_t pressed_at = 0;
    for (;;) {
        int received = uart_read_bytes(UART_NUM_0, &key, 1, pdMS_TO_TICKS(20));
        qemu_flush_pending_click(&pending, &pending_button, &pressed_at);
        if (received != 1) {
            continue;
        }
        if (key == 'p' || key == 'P') {
            if (pending) qemu_emit_button(pending_button, BSP_BTN_CLICK);
            pending = false;
            ESP_LOGI(TAG, "QEMU virtual power key: restarting");
            vTaskDelay(pdMS_TO_TICKS(50));
            esp_restart();
            continue;
        }
        bsp_btn_t button;
        if (qemu_decode_button(key, &escape_state, &discard_lf, &button)) {
            qemu_handle_button(button, &pending, &pending_button, &pressed_at);
        }
    }
}

static void qemu_input_start(void)
{
    esp_err_t error = uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "QEMU UART input unavailable: %s", esp_err_to_name(error));
        return;
    }
    if (xTaskCreate(qemu_input_task, "qemu_input", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "QEMU input task creation failed");
    } else {
        ESP_LOGI(TAG, "QEMU input: Up/Down/Enter, double press for double click, p=power reset");
    }
}
#endif

static void screen_idle_tick(lv_timer_t *timer) {
    (void)timer;
    uint32_t timeout_ms = work_assistant_screen_idle_timeout_ms();
    if (!s_screen_awake || timeout_ms == 0 ||
        lv_tick_elaps(s_last_input_tick) < timeout_ms) {
        return;
    }

    bsp_display_backlight(0);
    s_screen_awake = false;
    s_wake_guard = false;
    ESP_LOGI(TAG, "%lu ms 无按键操作,关闭背光", (unsigned long)timeout_ms);
}

// 熄屏后的第一次按键只负责唤醒。继续吞掉同一次按压产生的 CLICK/LONG，
// 避免亮屏同时触发页面操作；超时保护防止异常事件序列永久屏蔽输入。
static bool screen_idle_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    s_last_input_tick = lv_tick_get();
    if (!s_screen_awake) {
        bsp_display_backlight(100);
        s_screen_awake = true;
        s_wake_guard = (ev == BSP_BTN_PRESS);
        s_wake_button = btn;
        s_wake_guard_tick = s_last_input_tick;
        ESP_LOGI(TAG, "按键唤醒屏幕");
        return true;
    }

    if (!s_wake_guard) return false;
    if (lv_tick_elaps(s_wake_guard_tick) >= SCREEN_WAKE_GUARD_MS) {
        s_wake_guard = false;
        return false;
    }
    if (btn == s_wake_button &&
        (ev == BSP_BTN_CLICK || ev == BSP_BTN_DOUBLE || ev == BSP_BTN_LONG)) {
        s_wake_guard = false;
    }
    return true;
}

static void menu_refresh(void) {
    for (size_t i = 0; i < DEMO_COUNT; i++) {
        lv_label_set_text_fmt(s_rows[i], "%s%s",
                              DEMOS[i].name,
                              s_ok[i] ? "" : "  [FAIL]");
        ui_pixel_set_selected(s_cards[i], (int)i == s_sel, s_ok[i]);
        lv_obj_set_style_text_color(s_rows[i],
            s_ok[i] ? lv_color_hex(UI_INK) : lv_color_hex(0x7A2020), 0);
    }
}

static void menu_build(void) {
    s_menu_scr = ui_pixel_screen_create("FoloToy");

    for (size_t i = 0; i < DEMO_COUNT; i++) {
        int x = 11 + (int)(i % 2) * 112;
        int y = 52 + (int)(i / 2) * 47;
        s_cards[i] = ui_pixel_panel_create(s_menu_scr, x, y, 102, 40, UI_PAPER);
        s_rows[i] = lv_label_create(s_cards[i]);
        lv_obj_set_style_text_font(s_rows[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(s_rows[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(s_rows[i]);
    }

    s_mascot = ui_pixel_mascot_create(s_menu_scr, 101, 242);

    menu_refresh();
    lv_screen_load(s_menu_scr);
}

static void enter_menu(void) {
    s_active = -1;
    s_work_assistant_active = false;
    menu_build();
}

// 按键回调运行在 button 组件的任务里,操作 LVGL 必须加锁。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    if (!bsp_lvgl_lock(500)) return;
    if (screen_idle_handle_key(btn, ev)) {
        bsp_lvgl_unlock();
        return;
    }

    if (s_work_assistant_active) {
        if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
            work_assistant_exit();
            enter_menu();
        } else {
            work_assistant_key(btn, ev);
        }
    } else if (s_active >= 0) {
        if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {     // 统一返回
            DEMOS[s_active].exit();
            enter_menu();
        } else {
            DEMOS[s_active].key(btn, ev);
        }
    } else if (ev == BSP_BTN_CLICK) {
        if (btn == BSP_BTN_UP)   { s_sel = (s_sel + DEMO_COUNT - 1) % DEMO_COUNT; menu_refresh(); }
        if (btn == BSP_BTN_DOWN) { s_sel = (s_sel + 1) % DEMO_COUNT;              menu_refresh(); }
        if (btn == BSP_BTN_OK && s_ok[s_sel]) {
            s_active = s_sel;
            ui_pixel_mascot_jump(s_mascot);
            lv_obj_delete(s_menu_scr);
            s_menu_scr = NULL;
            s_mascot = NULL;
            DEMOS[s_active].enter();
        } else if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            ui_pixel_mascot_jump(s_mascot);
        }
    }
    bsp_lvgl_unlock();
}

void app_main(void) {
    ESP_LOGI(TAG, "FoloToy AI Passport BSP demo 启动");
    esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
    if (wakeup != ESP_SLEEP_WAKEUP_UNDEFINED) {
        ESP_LOGI(TAG, "休眠唤醒原因: %d", wakeup);
    }

#if CONFIG_AI_PASSPORT_QEMU
    ESP_LOGI(TAG, "QEMU startup config: physical I2C/I2S skipped");
#else
    bsp_i2c_init();
    bsp_i2c_scan();
#endif

    // 屏幕是本 demo 的 UI 载体,失败就没有菜单可言 —— 打清楚日志后退出,
    // 不做"串口菜单"降级(那会让本文件复杂一倍,违背参考示例的初衷)。
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败,demo 无法继续。"
                      "检查 SPI 接线(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    // 其余外设单项失败不阻塞:菜单里标 [FAIL],其他项照常可测。
    s_ok[0] = true;                                   // Display 已确认可用
    s_ok[1] = (bsp_button_init(on_key, NULL) == ESP_OK);
#if CONFIG_AI_PASSPORT_QEMU
    s_ok[2] = (bsp_audio_init() == ESP_OK);
    s_ok[3] = false;
    s_ok[4] = true;
#else
    s_ok[2] = (bsp_audio_init() == ESP_OK);
    s_ok[3] = (bsp_battery_init() == ESP_OK);
    s_ok[4] = true;                                    // BLE 页面内按需初始化并显示错误
#endif
    s_ok[5] = true;                                    // Low Power

#if CONFIG_AI_PASSPORT_QEMU
    qemu_input_start();
#endif

    if (bsp_lvgl_lock(1000)) {
        s_last_input_tick = lv_tick_get();
        if (s_ok[1] && !CONFIG_AI_PASSPORT_QEMU) {
            s_screen_idle_timer = lv_timer_create(screen_idle_tick, 1000, NULL);
            if (!s_screen_idle_timer) {
                ESP_LOGW(TAG, "无法创建自动熄屏计时器");
            }
        }
        s_work_assistant_active = true;
        work_assistant_enter();
        bsp_lvgl_unlock();
    }

    ESP_LOGI(TAG, "就绪:Display=%d Button=%d Audio=%d Battery=%d",
             s_ok[0], s_ok[1], s_ok[2], s_ok[3]);
}
