#include "work_assistant.h"

#include "bsp_battery.h"
#include "demo_radio.h"
#include "lvgl.h"
#include "passport_ble_bridge.h"
#include "work_assistant_flow.h"
#include "work_assistant_model.h"
#include "work_assistant_settings.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <ctype.h>
#include <inttypes.h>
#include <nvs.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

LV_FONT_DECLARE(lv_font_passport_zh_14);

#define WA_ROWS 3
#define WA_TITLE_SCROLL_PIXELS_PER_SECOND 24U
#define WA_NVS_RESTORE_WAIT_MS 1500U
#define WA_SETTINGS_NAMESPACE "wa_settings"
#define WA_SETTINGS_AUTO_OFF_KEY "auto_off"
#define WA_SETTINGS_NIGHT_MODE_KEY "night_mode"
#define WA_SETTINGS_ITEM_COUNT 4U
#define WA_BRIDGE_TASK_STACK 4096U

static const char *TAG = "work_assistant";

typedef enum {
    WA_HOME_SELECTION_AVATAR = 0,
    WA_HOME_SELECTION_SUMMARY,
    WA_HOME_SELECTION_ITEM,
} work_assistant_home_selection_t;

typedef enum {
    WA_TASK_DETAIL_ACTION_COMPLETE = 0,
    WA_TASK_DETAIL_ACTION_BACK,
} work_assistant_task_detail_action_t;

typedef struct {
    uint32_t background;
    uint32_t text;
    uint32_t muted;
    uint32_t border;
    uint32_t blue;
    uint32_t event_text;
    uint32_t purple;
    uint32_t green;
    uint32_t done_bg;
    uint32_t done_text;
    uint32_t status_bg;
    uint32_t now;
    uint32_t surface;
    uint32_t neutral;
    uint32_t blue_soft;
    uint32_t purple_soft;
    uint32_t focus_blue;
    uint32_t row_alt;
    uint32_t done_accent;
    uint32_t avatar_text;
} work_assistant_theme_t;

static const work_assistant_theme_t THEMES[] = {
    {
        .background = 0xF7F8FC,
        .text = 0x1F2329,
        .muted = 0x8F959E,
        .border = 0xE2E6EF,
        .blue = 0x3370FF,
        .event_text = 0x1456F0,
        .purple = 0x7B61FF,
        .green = 0x00B578,
        .done_bg = 0xEAF0FF,
        .done_text = 0x94A4C6,
        .status_bg = 0xF0F4FF,
        .now = 0xF54A45,
        .surface = 0xFFFFFF,
        .neutral = 0xF2F3F5,
        .blue_soft = 0xECF3FF,
        .purple_soft = 0xF4F1FF,
        .focus_blue = 0xEEF4FF,
        .row_alt = 0xF5F3FF,
        .done_accent = 0xAFC4F5,
        .avatar_text = 0xFFFFFF,
    },
    {
        .background = 0x121417,
        .text = 0xF2F3F5,
        .muted = 0xA7ADB8,
        .border = 0x3A3F49,
        .blue = 0x78A2FF,
        .event_text = 0x9AB9FF,
        .purple = 0xB09BFF,
        .green = 0x48D6A2,
        .done_bg = 0x242830,
        .done_text = 0x858E9E,
        .status_bg = 0x1B2028,
        .now = 0xFF6B66,
        .surface = 0x1A1D22,
        .neutral = 0x22262D,
        .blue_soft = 0x1C2940,
        .purple_soft = 0x29243A,
        .focus_blue = 0x1E2B43,
        .row_alt = 0x262238,
        .done_accent = 0x596579,
        .avatar_text = 0xFFFFFF,
    },
};

static lv_obj_t *s_scr;
static lv_obj_t *s_avatar;
static lv_obj_t *s_avatar_initial;
static lv_obj_t *s_avatar_image;
static lv_obj_t *s_name;
static lv_obj_t *s_battery;
static lv_obj_t *s_page_detail;
static lv_obj_t *s_ble_pairing_code;
static lv_obj_t *s_ble_caption;
static lv_obj_t *s_mode_ble;
static lv_obj_t *s_focus;
static lv_obj_t *s_status_task;
static lv_obj_t *s_status_message;
static lv_obj_t *s_list_title;
static lv_obj_t *s_position;
static lv_obj_t *s_time_pointer;
static lv_obj_t *s_rows[WA_ROWS];
static lv_obj_t *s_row_accent[WA_ROWS];
static lv_obj_t *s_row_checkbox[WA_ROWS];
static lv_obj_t *s_row_time[WA_ROWS];
static lv_obj_t *s_row_title[WA_ROWS];
static lv_obj_t *s_settings_cards[WA_SETTINGS_ITEM_COUNT];
static lv_obj_t *s_settings_values[WA_SETTINGS_ITEM_COUNT];
static lv_timer_t *s_timer;
static TaskHandle_t s_bridge_task;
static volatile bool s_bridge_requested;
static work_assistant_flow_t s_flow;
static work_assistant_summary_t s_summary;
static size_t s_today_count;
static size_t s_event_first;
static size_t s_task_first;
static uint32_t s_tick_count;
static uint32_t s_ble_revision;
static uint32_t s_nvs_restore_started_at;
static size_t s_last_time_target;
static bool s_tasks_visible;
static bool s_night_mode;
static bool s_waiting_for_nvs_restore;
static bool s_settings_visible;
static bool s_task_detail_visible;
static work_assistant_auto_off_t s_auto_off_setting;
static work_assistant_home_selection_t s_home_selection;
static size_t s_selected_item;
static size_t s_settings_selection;
static work_assistant_task_detail_action_t s_task_detail_action;
static lv_image_dsc_t s_avatar_dsc;
static char s_completed_task_guids[WORK_ASSISTANT_MAX_TASKS]
                                  [WORK_ASSISTANT_TASK_GUID_MAX];
static size_t s_completed_task_count;
static char s_detail_task_guid[WORK_ASSISTANT_TASK_GUID_MAX];

static const work_assistant_theme_t *theme(void)
{
    return &THEMES[s_night_mode ? 1 : 0];
}

static void refresh_battery(void);

static lv_obj_t *make_box(lv_obj_t *parent, int x, int y, int w, int h,
                          uint32_t color, uint32_t border, int radius)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_bg_color(box, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, radius, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    return box;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font,
                            uint32_t color, int width)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return label;
}

static void set_scrolling_title(lv_obj_t *label, const char *text)
{
    lv_point_t size;
    lv_text_get_size(&size, text, &lv_font_passport_zh_14, 0, 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    uint32_t wait_width =
        lv_font_get_glyph_width(&lv_font_passport_zh_14, ' ', ' ') *
        LV_LABEL_WAIT_CHAR_COUNT;
    uint32_t distance = (uint32_t)size.x + wait_width;
    uint32_t duration =
        distance * 1000U / WA_TITLE_SCROLL_PIXELS_PER_SECOND;
    lv_obj_set_style_anim_duration(label, duration, 0);
    lv_label_set_text(label, text);
}

static void draw_time_pointer(lv_event_t *event)
{
    lv_obj_t *object = lv_event_get_target(event);
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    lv_draw_triangle_dsc_t descriptor;
    lv_draw_triangle_dsc_init(&descriptor);
    descriptor.color = lv_color_hex(theme()->now);
    descriptor.opa = LV_OPA_COVER;
    descriptor.p[0] = (lv_point_precise_t) { area.x1, (area.y1 + area.y2) / 2 };
    descriptor.p[1] = (lv_point_precise_t) { area.x2, area.y1 };
    descriptor.p[2] = (lv_point_precise_t) { area.x2, area.y2 };
    lv_draw_triangle(lv_event_get_layer(event), &descriptor);
}

static void avatar_initial(char text[5])
{
    const unsigned char *name = (const unsigned char *)s_summary.account_name;
    if (!name[0]) {
        strcpy(text, "F");
        return;
    }
    if (name[0] < 0x80U) {
        text[0] = isalpha(name[0]) ? (char)toupper(name[0]) : (char)name[0];
        text[1] = '\0';
        return;
    }
    size_t length = (name[0] & 0xF0U) == 0xF0U ? 4U :
                    (name[0] & 0xE0U) == 0xE0U ? 3U : 2U;
    memcpy(text, name, length);
    text[length] = '\0';
}

static void refresh_avatar(void)
{
    if (!s_avatar_initial || !s_avatar_image) return;
    if (!s_summary.avatar_valid) {
        char text[5];
        avatar_initial(text);
        lv_label_set_text(s_avatar_initial, text);
        lv_obj_remove_flag(s_avatar_initial, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_avatar_image, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    s_avatar_dsc = (lv_image_dsc_t) {
        .header = {
            .cf = LV_COLOR_FORMAT_RGB565,
            .w = WORK_ASSISTANT_AVATAR_SIZE,
            .h = WORK_ASSISTANT_AVATAR_SIZE,
            .stride = WORK_ASSISTANT_AVATAR_SIZE * 2,
        },
        .data_size = WORK_ASSISTANT_AVATAR_BYTES,
        .data = s_summary.avatar_rgb565,
    };
    lv_image_set_src(s_avatar_image, &s_avatar_dsc);
    lv_obj_invalidate(s_avatar_image);
    lv_obj_add_flag(s_avatar_initial, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_avatar_image, LV_OBJ_FLAG_HIDDEN);
}

static bool current_local_time(uint32_t *day_key, uint16_t *minute)
{
    time_t now = time(NULL);
    if (!s_summary.generated_at_epoch ||
        now < (time_t)s_summary.generated_at_epoch) {
        return false;
    }
    now += (time_t)s_summary.utc_offset_minutes * 60;
    struct tm local;
    if (!gmtime_r(&now, &local)) return false;
    *day_key = (uint32_t)(local.tm_year + 1900) * 10000U +
               (uint32_t)(local.tm_mon + 1) * 100U +
               (uint32_t)local.tm_mday;
    *minute = (uint16_t)(local.tm_hour * 60 + local.tm_min);
    return true;
}

static void load_settings(void)
{
    s_auto_off_setting = WORK_ASSISTANT_AUTO_OFF_1_MINUTE;
    s_night_mode = false;
    if (demo_radio_nvs_prepare() != ESP_OK) return;

    nvs_handle_t handle;
    if (nvs_open(WA_SETTINGS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;

    uint8_t value;
    if (nvs_get_u8(handle, WA_SETTINGS_AUTO_OFF_KEY, &value) == ESP_OK &&
        value < WORK_ASSISTANT_AUTO_OFF_COUNT) {
        s_auto_off_setting = (work_assistant_auto_off_t)value;
    }
    if (nvs_get_u8(handle, WA_SETTINGS_NIGHT_MODE_KEY, &value) == ESP_OK) {
        s_night_mode = value != 0;
    }
    nvs_close(handle);
}

static void save_settings(void)
{
    if (demo_radio_nvs_prepare() != ESP_OK) return;

    nvs_handle_t handle;
    if (nvs_open(WA_SETTINGS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    esp_err_t err = nvs_set_u8(handle, WA_SETTINGS_AUTO_OFF_KEY,
                               (uint8_t)s_auto_off_setting);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, WA_SETTINGS_NIGHT_MODE_KEY,
                         s_night_mode ? 1U : 0U);
    }
    if (err == ESP_OK) nvs_commit(handle);
    nvs_close(handle);
}

static void reset_widget_refs(void)
{
    s_avatar = NULL;
    s_battery = NULL;
    s_avatar_initial = NULL;
    s_avatar_image = NULL;
    s_name = NULL;
    s_page_detail = NULL;
    s_ble_pairing_code = NULL;
    s_ble_caption = NULL;
    s_mode_ble = NULL;
    s_focus = NULL;
    s_status_task = NULL;
    s_status_message = NULL;
    s_list_title = NULL;
    s_position = NULL;
    s_time_pointer = NULL;
    memset(s_settings_cards, 0, sizeof(s_settings_cards));
    memset(s_settings_values, 0, sizeof(s_settings_values));
    memset(s_rows, 0, sizeof(s_rows));
    memset(s_row_accent, 0, sizeof(s_row_accent));
    memset(s_row_checkbox, 0, sizeof(s_row_checkbox));
    memset(s_row_time, 0, sizeof(s_row_time));
    memset(s_row_title, 0, sizeof(s_row_title));
}

static void clear_screen(void)
{
    lv_obj_clean(s_scr);
    reset_widget_refs();
}

static void build_step_header(const char *step, const char *title, const char *detail)
{
    const work_assistant_theme_t *colors = theme();
    lv_obj_t *step_label = make_label(s_scr, &lv_font_montserrat_14, colors->blue, 130);
    lv_obj_set_pos(step_label, 14, 13);
    lv_label_set_text(step_label, step);

    s_battery = make_label(s_scr, &lv_font_montserrat_14, colors->muted, 48);
    lv_obj_set_style_text_align(s_battery, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_battery, 178, 13);

    lv_obj_t *title_label = make_label(s_scr, &lv_font_montserrat_14, colors->text, 212);
    lv_obj_set_pos(title_label, 14, 42);
    lv_label_set_text(title_label, title);

    s_page_detail = make_label(s_scr, &lv_font_montserrat_14, colors->muted, 212);
    lv_obj_set_pos(s_page_detail, 14, 67);
    lv_label_set_long_mode(s_page_detail, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_page_detail, detail);
}

static void refresh_launch_selection(void)
{
    const work_assistant_theme_t *colors = theme();
    lv_obj_set_style_bg_color(s_mode_ble, lv_color_hex(colors->purple_soft), 0);
    lv_obj_set_style_border_color(s_mode_ble, lv_color_hex(colors->purple), 0);
    lv_obj_set_style_border_width(s_mode_ble, 2, 0);
}

static void build_launch_screen(void)
{
    const work_assistant_theme_t *colors = theme();
    clear_screen();
    build_step_header("START", "Initialize Passport",
                      "Pair a computer to send your first work-data update.");

    s_mode_ble = make_box(s_scr, 14, 133, 212, 64,
                          colors->purple_soft, colors->purple, 7);
    lv_obj_t *ble_title = make_label(s_mode_ble, &lv_font_montserrat_14,
                                     colors->text, 150);
    lv_obj_set_pos(ble_title, 15, 12);
    lv_label_set_text(ble_title, "BLE bridge");
    lv_obj_t *ble_detail = make_label(s_mode_ble, &lv_font_montserrat_14,
                                      colors->muted, 150);
    lv_obj_set_pos(ble_detail, 15, 36);
    lv_label_set_text(ble_detail, "Available");

    lv_obj_t *footer = make_label(s_scr, &lv_font_montserrat_14, colors->muted, 212);
    lv_obj_set_style_text_align(footer, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(footer, 14, 277);
    lv_label_set_text(footer, "Bluetooth pairing");
    refresh_launch_selection();
}

static void build_ble_screen(void)
{
    const work_assistant_theme_t *colors = theme();
    clear_screen();
    build_step_header("BLUETOOTH", "Connect computer",
                      "Waiting for the Passport bridge.");
    lv_obj_t *device = make_box(s_scr, 14, 112, 212, 104,
                                colors->blue_soft, colors->blue, 7);
    lv_obj_t *device_name = make_label(device, &lv_font_montserrat_14,
                                       colors->text, 184);
    lv_obj_set_style_text_align(device_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(device_name, 14, 10);
    lv_label_set_text(device_name, PASSPORT_BLE_DEVICE_NAME);
    s_ble_pairing_code = make_label(device, &lv_font_montserrat_20,
                                    colors->text, 184);
    lv_obj_set_style_text_align(s_ble_pairing_code, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_outline_stroke_color(
        s_ble_pairing_code, lv_color_hex(colors->text), 0);
    lv_obj_set_style_text_outline_stroke_width(s_ble_pairing_code, 1, 0);
    lv_obj_set_pos(s_ble_pairing_code, 14, 31);
    lv_label_set_text(s_ble_pairing_code, "");
    s_ble_caption = make_label(device, &lv_font_montserrat_14,
                               colors->muted, 184);
    lv_obj_set_style_text_align(s_ble_caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_ble_caption, 14, 69);
    lv_label_set_text(s_ble_caption, "Work-data bridge");
}

static void refresh_focus(void)
{
    if (!s_focus) return;
    const work_assistant_theme_t *colors = theme();
    bool selected = s_home_selection == WA_HOME_SELECTION_SUMMARY;
    lv_obj_set_style_border_color(s_focus,
                                  lv_color_hex(s_tasks_visible
                                                   ? colors->purple
                                                   : colors->blue), 0);
    lv_obj_set_style_border_width(s_focus, 1, 0);
    lv_obj_set_style_bg_color(s_focus,
                              lv_color_hex(selected
                                               ? (s_tasks_visible
                                                      ? colors->purple_soft
                                                      : colors->focus_blue)
                                               : colors->status_bg), 0);
    if (s_avatar) {
        lv_obj_set_style_border_color(s_avatar,
                                      lv_color_hex(colors->purple), 0);
        lv_obj_set_style_border_width(
            s_avatar, s_home_selection == WA_HOME_SELECTION_AVATAR ? 1 : 0, 0);
    }
}

static void refresh_settings_selection(void)
{
    const work_assistant_theme_t *colors = theme();
    for (size_t index = 0; index < WA_SETTINGS_ITEM_COUNT; index++) {
        if (!s_settings_cards[index]) continue;
        bool selected = index == s_settings_selection;
        lv_obj_set_style_border_color(s_settings_cards[index],
                                      lv_color_hex(selected
                                                       ? colors->purple
                                                       : colors->border), 0);
        lv_obj_set_style_border_width(s_settings_cards[index], 1, 0);
        lv_obj_set_style_bg_color(s_settings_cards[index],
                                  lv_color_hex(selected
                                                   ? colors->purple_soft
                                                   : colors->surface), 0);
    }
}

static void refresh_settings_values(void)
{
    if (!s_settings_values[0]) return;
    lv_label_set_text(s_settings_values[0],
                      work_assistant_auto_off_label(s_auto_off_setting));
    lv_label_set_text(s_settings_values[1], s_night_mode ? "Dark" : "Light");
    char serial[13];
    lv_label_set_text(s_settings_values[2],
                      passport_ble_bridge_serial_number(serial) ? serial : "--");
    lv_label_set_text_fmt(s_settings_values[3], "v%u",
                          passport_ble_bridge_protocol_version());
}

static void format_task_date(const work_assistant_task_t *task, char out[6])
{
    time_t local_epoch = (time_t)task->due_at_epoch +
                         (time_t)s_summary.utc_offset_minutes * 60;
    struct tm due;
    if (!gmtime_r(&local_epoch, &due)) {
        strlcpy(out, "-- --", 6);
        return;
    }
    snprintf(out, 6, "%02u-%02u", (unsigned)(due.tm_mon + 1),
             (unsigned)due.tm_mday);
}

static size_t active_item_count(void)
{
    return s_tasks_visible ? s_summary.task_count : s_today_count;
}

static bool task_is_locally_completed(const char *guid)
{
    if (!guid || !guid[0]) return false;
    for (size_t index = 0; index < s_completed_task_count; index++) {
        if (!strcmp(guid, s_completed_task_guids[index])) return true;
    }
    return false;
}

static bool set_task_locally_completed(const char *guid, bool completed)
{
    if (!guid || !guid[0]) return false;

    for (size_t index = 0; index < s_completed_task_count; index++) {
        if (strcmp(guid, s_completed_task_guids[index])) continue;
        if (completed) return true;
        memmove(&s_completed_task_guids[index], &s_completed_task_guids[index + 1],
                (s_completed_task_count - index - 1) *
                    sizeof(s_completed_task_guids[0]));
        s_completed_task_count--;
        memset(s_completed_task_guids[s_completed_task_count], 0,
               sizeof(s_completed_task_guids[0]));
        return true;
    }

    if (!completed || s_completed_task_count >= WORK_ASSISTANT_MAX_TASKS) return false;
    strlcpy(s_completed_task_guids[s_completed_task_count++], guid,
            WORK_ASSISTANT_TASK_GUID_MAX);
    return true;
}

static work_assistant_task_t *find_task_by_guid(const char *guid)
{
    if (!guid || !guid[0]) return NULL;
    for (size_t index = 0; index < s_summary.task_count; index++) {
        if (!strcmp(guid, s_summary.tasks[index].guid)) return &s_summary.tasks[index];
    }
    return NULL;
}

static void reconcile_completed_tasks(void)
{
    size_t kept = 0;
    for (size_t completed = 0; completed < s_completed_task_count; completed++) {
        bool found = false;
        for (size_t task = 0; task < s_summary.task_count; task++) {
            if (!strcmp(s_completed_task_guids[completed],
                        s_summary.tasks[task].guid)) {
                found = true;
                break;
            }
        }
        if (found) {
            if (kept != completed) {
                memcpy(s_completed_task_guids[kept],
                       s_completed_task_guids[completed],
                       sizeof(s_completed_task_guids[0]));
            }
            kept++;
        }
    }
    s_completed_task_count = kept;
}

static void ensure_selected_item_visible(size_t count, size_t *first)
{
    if (s_home_selection != WA_HOME_SELECTION_ITEM) return;
    if (s_selected_item >= count) {
        s_home_selection = WA_HOME_SELECTION_SUMMARY;
        return;
    }
    if (s_selected_item < *first) {
        *first = s_selected_item;
    } else if (s_selected_item >= *first + WA_ROWS) {
        *first = s_selected_item - WA_ROWS + 1;
    }
}

static void refresh_rows(void)
{
    if (!s_position) return;
    const work_assistant_theme_t *colors = theme();
    if (s_tasks_visible) {
        ensure_selected_item_visible(s_summary.task_count, &s_task_first);
        work_assistant_window_t window =
            work_assistant_window(s_summary.task_count, s_task_first, WA_ROWS);
        s_task_first = window.first;
        if (s_time_pointer) lv_obj_add_flag(s_time_pointer, LV_OBJ_FLAG_HIDDEN);
        for (size_t row = 0; row < WA_ROWS; row++) {
            if (row >= window.count) {
                lv_obj_add_flag(s_rows[row], LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            lv_obj_remove_flag(s_rows[row], LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(s_row_checkbox[row], LV_OBJ_FLAG_HIDDEN);
            work_assistant_task_t *task = &s_summary.tasks[window.first + row];
            char due[6];
            format_task_date(task, due);
            lv_label_set_text(s_row_time[row], due);
            lv_obj_set_style_text_font(s_row_time[row], &lv_font_montserrat_14, 0);
            bool completed = task_is_locally_completed(task->guid);
            set_scrolling_title(s_row_title[row], task->title);
            if (s_row_checkbox[row]) {
                if (completed) lv_obj_add_state(s_row_checkbox[row], LV_STATE_CHECKED);
                else lv_obj_remove_state(s_row_checkbox[row], LV_STATE_CHECKED);
            }
            lv_obj_set_style_bg_color(s_rows[row],
                                      lv_color_hex(row % 2
                                                       ? colors->row_alt
                                                       : colors->purple_soft), 0);
            lv_obj_set_style_bg_color(s_row_accent[row],
                                      lv_color_hex(completed
                                                       ? colors->done_accent
                                                       : colors->purple), 0);
            lv_obj_set_style_text_color(s_row_time[row],
                                        lv_color_hex(completed
                                                         ? colors->done_text
                                                         : colors->purple), 0);
            lv_obj_set_style_text_color(s_row_title[row],
                                        lv_color_hex(completed
                                                         ? colors->done_text
                                                         : colors->text), 0);
            bool selected = s_home_selection == WA_HOME_SELECTION_ITEM &&
                            s_selected_item == window.first + row;
            lv_obj_set_style_border_color(s_rows[row],
                                          lv_color_hex(selected
                                                           ? colors->purple
                                                           : colors->border), 0);
            lv_obj_set_style_border_width(s_rows[row], 1, 0);
            lv_obj_set_width(s_row_accent[row], selected ? 5 : 4);
        }
        if (s_summary.task_count == 0) {
            lv_label_set_text(s_position, "近30天无待办");
        } else {
            lv_label_set_text_fmt(s_position, "%u-%u / %u",
                                  (unsigned)(window.first + 1),
                                  (unsigned)(window.first + window.count),
                                  (unsigned)s_summary.task_count);
        }
        refresh_focus();
        return;
    }

    uint32_t current_day = 0;
    uint16_t current_minute = 0;
    bool time_valid = current_local_time(&current_day, &current_minute);
    size_t target = WORK_ASSISTANT_NO_EVENT;
    if (time_valid && current_day == s_summary.today_key) {
        target = work_assistant_time_target(
            s_summary.events, s_today_count, current_minute);
    }
    if (target != s_last_time_target) {
        s_last_time_target = target;
        if (target != WORK_ASSISTANT_NO_EVENT &&
            (target < s_event_first || target >= s_event_first + WA_ROWS)) {
            s_event_first = target > 0 ? target - 1 : 0;
        }
    }
    ensure_selected_item_visible(s_today_count, &s_event_first);

    work_assistant_window_t window =
        work_assistant_window(s_today_count, s_event_first, WA_ROWS);
    s_event_first = window.first;
    if (s_time_pointer) lv_obj_add_flag(s_time_pointer, LV_OBJ_FLAG_HIDDEN);

    for (size_t row = 0; row < WA_ROWS; row++) {
        if (row >= window.count) {
            lv_obj_add_flag(s_rows[row], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(s_rows[row], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_row_checkbox[row], LV_OBJ_FLAG_HIDDEN);
        work_assistant_event_t *event = &s_summary.events[window.first + row];
        char time[6] = { 0 };
        if (event->all_day) {
            lv_label_set_text(s_row_time[row], "全天");
            lv_obj_set_style_text_font(s_row_time[row], &lv_font_passport_zh_14, 0);
        } else {
            work_assistant_format_time(event->start_minute, time);
            lv_label_set_text(s_row_time[row], time);
            lv_obj_set_style_text_font(s_row_time[row], &lv_font_montserrat_14, 0);
        }
        set_scrolling_title(s_row_title[row], event->title);
        bool completed = event->completed;
        if (time_valid && !event->all_day) {
            completed = current_day > event->day_key ||
                        (current_day == event->day_key &&
                         event->end_minute <= current_minute);
        }
        uint32_t bg = completed
                          ? colors->done_bg
                          : (row % 2 ? colors->row_alt : colors->blue_soft);
        uint32_t time_color = completed ? colors->done_text : colors->blue;
        uint32_t title_color = completed ? colors->done_text : colors->event_text;
        lv_obj_set_style_bg_color(s_rows[row], lv_color_hex(bg), 0);
        lv_obj_set_style_bg_color(s_row_accent[row],
                                  lv_color_hex(completed
                                                   ? colors->done_accent
                                                   : colors->blue), 0);
        lv_obj_set_style_text_color(s_row_time[row], lv_color_hex(time_color), 0);
        lv_obj_set_style_text_color(s_row_title[row], lv_color_hex(title_color), 0);
        bool selected = s_home_selection == WA_HOME_SELECTION_ITEM &&
                        s_selected_item == window.first + row;
        lv_obj_set_style_border_color(s_rows[row],
                                      lv_color_hex(selected
                                                       ? colors->blue
                                                       : colors->border), 0);
        lv_obj_set_style_border_width(s_rows[row], 1, 0);
        lv_obj_set_width(s_row_accent[row], selected ? 5 : 4);
        if (s_time_pointer && window.first + row == target) {
            lv_obj_set_pos(s_time_pointer, 221, 164 + (int)row * 48);
            lv_obj_remove_flag(s_time_pointer, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_today_count == 0) {
        lv_label_set_text(s_position, "今日无日程");
    } else {
        lv_label_set_text_fmt(s_position, "%u-%u / %u",
                              (unsigned)(window.first + 1),
                              (unsigned)(window.first + window.count),
                              (unsigned)s_today_count);
    }
    refresh_focus();
}

static void build_home_screen(void)
{
    const work_assistant_theme_t *colors = theme();
    clear_screen();
    s_avatar = make_box(s_scr, 14, 14, 32, 32,
                        colors->blue, colors->blue, 16);
    lv_obj_set_style_border_width(s_avatar, 0, 0);
    lv_obj_set_style_clip_corner(s_avatar, true, 0);
    s_avatar_initial = make_label(s_avatar, &lv_font_passport_zh_14,
                                  colors->avatar_text, 35);
    lv_obj_set_style_text_align(s_avatar_initial, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_avatar_initial);
    s_avatar_image = lv_image_create(s_avatar);
    lv_obj_center(s_avatar_image);
    refresh_avatar();

    s_name = make_label(s_scr, &lv_font_passport_zh_14, colors->text, 116);
    lv_obj_set_pos(s_name, 56, 14);
    lv_label_set_text(s_name, s_summary.account_name);
    lv_obj_t *date = make_label(s_scr, &lv_font_montserrat_14, colors->muted, 116);
    lv_obj_set_pos(date, 56, 34);
    lv_label_set_text_fmt(date, "%u-%02u-%02u",
                          (unsigned)(s_summary.today_key / 10000U),
                          (unsigned)((s_summary.today_key / 100U) % 100U),
                          (unsigned)(s_summary.today_key % 100U));
    s_battery = make_label(s_scr, &lv_font_montserrat_14, colors->muted, 40);
    lv_obj_set_style_text_align(s_battery, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_battery, 188, 16);

    s_focus = make_box(s_scr, 12, 60, 216, 49,
                       colors->status_bg, colors->blue, 7);
    s_status_task = make_label(s_focus, &lv_font_passport_zh_14, colors->text, 92);
    lv_obj_set_pos(s_status_task, 13, 8);
    lv_label_set_text_fmt(s_status_task, "%u 待办", s_summary.overdue_task_count);
    lv_obj_t *task_caption = make_label(s_focus, &lv_font_passport_zh_14,
                                        colors->muted, 92);
    lv_obj_set_pos(task_caption, 13, 28);
    lv_label_set_text(task_caption, "近 30 天");
    make_box(s_focus, 108, 9, 1, 31, colors->border, colors->border, 0);
    s_status_message = make_label(s_focus, &lv_font_passport_zh_14,
                                  colors->text, 90);
    lv_obj_set_pos(s_status_message, 122, 8);
    lv_label_set_text_fmt(s_status_message, "%u 未读", s_summary.unread_message_count);
    lv_obj_t *message_caption = make_label(s_focus, &lv_font_passport_zh_14,
                                           colors->muted, 90);
    lv_obj_set_pos(message_caption, 122, 28);
    lv_label_set_text(message_caption, "消息");

    s_list_title = make_label(s_scr, &lv_font_passport_zh_14,
                              colors->text, 140);
    lv_obj_set_pos(s_list_title, 13, 122);
    lv_label_set_text(s_list_title, s_tasks_visible ? "近 30 天待办" : "今日日程");
    s_position = make_label(s_scr, &lv_font_passport_zh_14, colors->muted, 76);
    lv_obj_set_style_text_align(s_position, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_position, 151, 125);

    for (size_t i = 0; i < WA_ROWS; i++) {
        int y = 149 + (int)i * 48;
        s_rows[i] = make_box(s_scr, 12, y, 216, 42,
                             colors->blue_soft, colors->border, 6);
        s_row_accent[i] = make_box(s_rows[i], 0, 0, 4, 42,
                                   colors->blue, colors->blue, 6);
        s_row_time[i] = make_label(s_rows[i], &lv_font_montserrat_14,
                                   colors->blue, 42);
        lv_obj_set_pos(s_row_time[i], 11, 12);
        s_row_checkbox[i] = lv_checkbox_create(s_rows[i]);
        lv_checkbox_set_text(s_row_checkbox[i], "");
        lv_obj_remove_flag(s_row_checkbox[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(s_row_checkbox[i], 17, 17);
        lv_obj_set_pos(s_row_checkbox[i], 54, 12);
        lv_obj_set_style_pad_all(s_row_checkbox[i], 0, 0);
        lv_obj_set_style_border_width(s_row_checkbox[i], 1, LV_PART_INDICATOR);
        lv_obj_set_style_border_color(s_row_checkbox[i],
                                      lv_color_hex(colors->purple),
                                      LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(s_row_checkbox[i], LV_OPA_TRANSP,
                                LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s_row_checkbox[i], lv_color_hex(colors->purple),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(s_row_checkbox[i], LV_OPA_COVER,
                                LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(s_row_checkbox[i], lv_color_hex(0xFFFFFF),
                                    LV_PART_INDICATOR | LV_STATE_CHECKED);
        s_row_title[i] = make_label(s_rows[i], &lv_font_passport_zh_14,
                                    colors->text, 124);
        lv_obj_set_pos(s_row_title[i], 78, 11);
        lv_label_set_long_mode(s_row_title[i], LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    }
    s_time_pointer = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_time_pointer);
    lv_obj_set_size(s_time_pointer, 7, 12);
    lv_obj_add_event_cb(s_time_pointer, draw_time_pointer, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_flag(s_time_pointer, LV_OBJ_FLAG_HIDDEN);
    refresh_focus();
    refresh_rows();
}

static void build_task_detail_screen(void)
{
    const work_assistant_theme_t *colors = theme();
    work_assistant_task_t *task = find_task_by_guid(s_detail_task_guid);
    if (!task) {
        s_task_detail_visible = false;
        s_detail_task_guid[0] = '\0';
        build_home_screen();
        return;
    }

    clear_screen();
    lv_obj_t *title = make_label(s_scr, &lv_font_passport_zh_14, colors->text, 150);
    lv_obj_set_pos(title, 14, 18);
    lv_label_set_text(title, "任务详情");
    s_battery = make_label(s_scr, &lv_font_montserrat_14, colors->muted, 48);
    lv_obj_set_style_text_align(s_battery, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_battery, 178, 18);

    lv_obj_t *detail = make_box(s_scr, 12, 60, 216, 169,
                                colors->surface, colors->border, 7);
    lv_obj_t *task_title = make_label(detail, &lv_font_passport_zh_14,
                                      colors->text, 188);
    lv_obj_set_pos(task_title, 14, 15);
    lv_label_set_long_mode(task_title, LV_LABEL_LONG_WRAP);
    lv_label_set_text(task_title, task->title);

    char due[32];
    time_t local_epoch = (time_t)task->due_at_epoch +
                         (time_t)s_summary.utc_offset_minutes * 60;
    struct tm due_date;
    if (gmtime_r(&local_epoch, &due_date)) {
        snprintf(due, sizeof(due), "截止 %04u-%02u-%02u%s",
                 (unsigned)(due_date.tm_year + 1900),
                 (unsigned)(due_date.tm_mon + 1),
                 (unsigned)due_date.tm_mday,
                 task->all_day ? " 全天" : "");
    } else {
        strlcpy(due, "截止日期未知", sizeof(due));
    }
    lv_obj_t *due_label = make_label(detail, &lv_font_passport_zh_14,
                                     colors->muted, 188);
    lv_obj_set_pos(due_label, 14, 120);
    lv_label_set_text(due_label, due);
    lv_obj_t *state = make_label(detail, &lv_font_passport_zh_14,
                                 task_is_locally_completed(task->guid)
                                     ? colors->green : colors->purple,
                                 188);
    lv_obj_set_pos(state, 14, 143);
    lv_label_set_text(state, task_is_locally_completed(task->guid)
                             ? "已完成"
                             : "待完成");

    bool completed = task_is_locally_completed(task->guid);
    const char *labels[] = { completed ? "取消完成" : "完成", "返回" };
    for (size_t index = 0; index < 2; index++) {
        bool selected = s_task_detail_action == index;
        lv_obj_t *button = make_box(s_scr, 12 + (int)index * 110, 252, 106, 45,
                                    selected ? colors->purple_soft : colors->surface,
                                    selected ? colors->purple : colors->border, 7);
        lv_obj_t *label = make_label(button, &lv_font_passport_zh_14,
                                     selected ? colors->purple : colors->text, 92);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
        lv_label_set_text(label, labels[index]);
    }
    refresh_battery();
}

static void build_settings_screen(void)
{
    const work_assistant_theme_t *colors = theme();
    clear_screen();

    lv_obj_t *title = make_label(s_scr, &lv_font_passport_zh_14, colors->text, 150);
    lv_obj_set_pos(title, 14, 18);
    lv_label_set_text(title, "设置");
    s_battery = make_label(s_scr, &lv_font_montserrat_14, colors->muted, 48);
    lv_obj_set_style_text_align(s_battery, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_battery, 178, 18);

    static const char *const labels[WA_SETTINGS_ITEM_COUNT] = {
        "自动熄屏",
        "显示模式",
        "蓝牙序列号",
        "蓝牙协议",
    };
    for (size_t index = 0; index < WA_SETTINGS_ITEM_COUNT; index++) {
        int y = 62 + (int)index * 57;
        s_settings_cards[index] = make_box(s_scr, 12, y, 216, 49,
                                           colors->surface, colors->border, 7);
        lv_obj_t *label = make_label(s_settings_cards[index],
                                     &lv_font_passport_zh_14, colors->text, 110);
        lv_obj_set_pos(label, 13, 15);
        lv_label_set_text(label, labels[index]);
        s_settings_values[index] = make_label(s_settings_cards[index],
                                              &lv_font_montserrat_14,
                                              colors->muted, 88);
        lv_obj_set_style_text_align(s_settings_values[index], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(s_settings_values[index], 114, 15);
    }
    refresh_settings_values();
    refresh_settings_selection();
}

static void refresh_battery(void)
{
    if (!s_battery) return;
    int soc = bsp_battery_soc();
    if (soc < 0) lv_label_set_text(s_battery, "");
    else lv_label_set_text_fmt(s_battery, "%d%%", soc);
}

static void rebuild_current_screen(void)
{
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(theme()->background), 0);
    if (s_settings_visible) {
        build_settings_screen();
        refresh_battery();
        return;
    }
    switch (s_flow.screen) {
        case WORK_ASSISTANT_SCREEN_LAUNCH:
            build_launch_screen();
            break;
        case WORK_ASSISTANT_SCREEN_BLE:
            build_ble_screen();
            break;
        case WORK_ASSISTANT_SCREEN_HOME:
            if (s_task_detail_visible) build_task_detail_screen();
            else build_home_screen();
            break;
    }
    refresh_battery();
}

static void refresh_home_summary(void)
{
    if (s_name) lv_label_set_text(s_name, s_summary.account_name);
    s_last_time_target = WORK_ASSISTANT_NO_EVENT;
    work_assistant_sort_tasks(s_summary.tasks, s_summary.task_count);
    refresh_avatar();
    if (s_status_task) {
        lv_label_set_text_fmt(s_status_task, "%u 待办", s_summary.overdue_task_count);
    }
    if (s_status_message) {
        lv_label_set_text_fmt(s_status_message, "%u 未读",
                              s_summary.unread_message_count);
    }
    if (s_task_detail_visible) {
        if (find_task_by_guid(s_detail_task_guid)) build_task_detail_screen();
        else {
            s_task_detail_visible = false;
            s_detail_task_guid[0] = '\0';
            build_home_screen();
        }
    } else {
        refresh_rows();
    }
}

static void refresh_flow(lv_timer_t *timer)
{
    (void)timer;
    if ((s_tick_count++ % 60U) == 0U) refresh_battery();
    if (s_settings_visible) {
        refresh_settings_values();
        return;
    }

    if (s_waiting_for_nvs_restore) {
        passport_ble_bridge_state_t state = passport_ble_bridge_state();
        if (state == PASSPORT_BLE_BRIDGE_READY &&
            passport_ble_bridge_copy_summary(&s_summary)) {
            s_waiting_for_nvs_restore = false;
            s_ble_revision = passport_ble_bridge_revision();
            s_today_count = work_assistant_prepare_today(
                s_summary.events, s_summary.event_count, s_summary.today_key);
            work_assistant_sort_tasks(s_summary.tasks, s_summary.task_count);
            reconcile_completed_tasks();
            s_event_first = 0;
            s_task_first = 0;
            s_flow.screen = WORK_ASSISTANT_SCREEN_HOME;
            build_home_screen();
            refresh_battery();
        } else if (state == PASSPORT_BLE_BRIDGE_PAIRING ||
                   state == PASSPORT_BLE_BRIDGE_ERROR ||
                   lv_tick_elaps(s_nvs_restore_started_at) >=
                       WA_NVS_RESTORE_WAIT_MS) {
            s_waiting_for_nvs_restore = false;
            work_assistant_flow_confirm(&s_flow);
            build_ble_screen();
            refresh_battery();
        }
        return;
    }

    if (s_flow.screen == WORK_ASSISTANT_SCREEN_BLE) {
        passport_ble_bridge_state_t state = passport_ble_bridge_state();
        if (state == PASSPORT_BLE_BRIDGE_READY &&
            passport_ble_bridge_copy_summary(&s_summary)) {
            s_ble_revision = passport_ble_bridge_revision();
            s_today_count = work_assistant_prepare_today(
                s_summary.events, s_summary.event_count, s_summary.today_key);
            work_assistant_sort_tasks(s_summary.tasks, s_summary.task_count);
            s_event_first = 0;
            s_task_first = 0;
            work_assistant_flow_show_home(&s_flow);
            build_home_screen();
            refresh_battery();
        } else if (s_page_detail) {
            uint32_t pairing_code;
            if (passport_ble_bridge_pairing_code(&pairing_code)) {
                lv_label_set_text_fmt(s_ble_pairing_code, "%06" PRIu32,
                                      pairing_code);
                lv_label_set_text(s_ble_caption, "Enter code on computer.");
                lv_label_set_text(s_page_detail,
                                  "Confirm the pairing request on your computer.");
            } else {
                lv_label_set_text(s_ble_pairing_code, "");
                lv_label_set_text(s_ble_caption, "Work-data bridge");
                lv_label_set_text(s_page_detail,
                                  passport_ble_bridge_status_message());
            }
        }
        return;
    }

    if (s_flow.screen == WORK_ASSISTANT_SCREEN_HOME) {
        passport_ble_bridge_state_t state = passport_ble_bridge_state();
        /*
         * Keep showing the last synchronized data while disconnected. Only a
         * host-initiated pairing request replaces the home screen so the user
         * can read and enter the new code.
         */
        if (state == PASSPORT_BLE_BRIDGE_PAIRING) {
            s_flow.screen = WORK_ASSISTANT_SCREEN_BLE;
            build_ble_screen();
            refresh_battery();
            return;
        }
        if (passport_ble_bridge_revision() != s_ble_revision &&
            state == PASSPORT_BLE_BRIDGE_READY &&
            passport_ble_bridge_copy_summary(&s_summary)) {
            s_ble_revision = passport_ble_bridge_revision();
            s_today_count = work_assistant_prepare_today(
                s_summary.events, s_summary.event_count, s_summary.today_key);
            reconcile_completed_tasks();
            refresh_home_summary();
        }
        if ((s_tick_count % 60U) == 0U) refresh_rows();
    }
}

static void bridge_lifecycle_task(void *arg)
{
    (void)arg;
    for (;;) {
        TickType_t wait = s_bridge_requested ? pdMS_TO_TICKS(500)
                                             : portMAX_DELAY;
        ulTaskNotifyTake(pdTRUE, wait);
        for (;;) {
            bool requested = s_bridge_requested;
            bool applied;
            if (requested) {
                applied = passport_ble_bridge_start();
                if (!applied) passport_ble_bridge_stop();
            } else {
                applied = passport_ble_bridge_stop();
            }
            if (applied && requested == s_bridge_requested) break;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

static bool request_bridge(bool running)
{
    if (!s_bridge_task) {
        if (!running) return true;
        if (xTaskCreate(bridge_lifecycle_task, "wa_bridge",
                        WA_BRIDGE_TASK_STACK, NULL, 4,
                        &s_bridge_task) != pdPASS) {
            s_bridge_task = NULL;
            ESP_LOGE(TAG, "Unable to create Work Assistant bridge task");
            return false;
        }
    }
    s_bridge_requested = running;
    xTaskNotifyGive(s_bridge_task);
    return true;
}

void work_assistant_enter(void)
{
    load_settings();
    work_assistant_load_demo(&s_summary);
    s_today_count = work_assistant_prepare_today(
        s_summary.events, s_summary.event_count, s_summary.today_key);
    work_assistant_sort_tasks(s_summary.tasks, s_summary.task_count);
    s_event_first = 0;
    s_task_first = 0;
    s_tick_count = 0;
    s_ble_revision = 0;
    s_nvs_restore_started_at = 0;
    s_last_time_target = WORK_ASSISTANT_NO_EVENT;
    s_tasks_visible = false;
    s_waiting_for_nvs_restore = false;
    s_settings_visible = false;
    s_task_detail_visible = false;
    s_home_selection = WA_HOME_SELECTION_AVATAR;
    s_selected_item = 0;
    s_settings_selection = 0;
    s_task_detail_action = WA_TASK_DETAIL_ACTION_COMPLETE;
    s_completed_task_count = 0;
    memset(s_completed_task_guids, 0, sizeof(s_completed_task_guids));
    memset(s_detail_task_guid, 0, sizeof(s_detail_task_guid));
    work_assistant_flow_init(&s_flow);

    s_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(theme()->background), 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    if (request_bridge(true)) {
        s_waiting_for_nvs_restore = true;
        s_nvs_restore_started_at = lv_tick_get();
    } else {
        /*
         * Bluetooth is the only exposed setup path. A Passport without a
         * committed transfer enters this screen immediately and advertises for
         * its first host pairing.
         */
        work_assistant_flow_confirm(&s_flow);
        build_ble_screen();
    }
    refresh_battery();
    s_timer = lv_timer_create(refresh_flow, 500, NULL);
    lv_screen_load(s_scr);
}

void work_assistant_exit(void)
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_waiting_for_nvs_restore = false;
    s_task_detail_visible = false;
    s_detail_task_guid[0] = '\0';
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
    }
    reset_widget_refs();
    request_bridge(false);
}

uint32_t work_assistant_screen_idle_timeout_ms(void)
{
    return work_assistant_auto_off_timeout_ms(s_auto_off_setting);
}

void work_assistant_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (btn == BSP_BTN_OK && ev == BSP_BTN_DOUBLE) {
        if (s_settings_visible) {
            s_settings_visible = false;
            rebuild_current_screen();
        }
        return;
    }
    if (ev != BSP_BTN_CLICK) return;
    if (s_flow.screen == WORK_ASSISTANT_SCREEN_LAUNCH) {
        if (btn == BSP_BTN_OK && work_assistant_flow_confirm(&s_flow)) {
            request_bridge(true);
            build_ble_screen();
            refresh_battery();
        }
        return;
    }

    if (s_flow.screen != WORK_ASSISTANT_SCREEN_HOME) return;
    if (s_task_detail_visible) {
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            s_task_detail_action =
                s_task_detail_action == WA_TASK_DETAIL_ACTION_COMPLETE
                    ? WA_TASK_DETAIL_ACTION_BACK
                    : WA_TASK_DETAIL_ACTION_COMPLETE;
            build_task_detail_screen();
            return;
        }
        if (btn != BSP_BTN_OK) return;

        if (s_task_detail_action == WA_TASK_DETAIL_ACTION_COMPLETE) {
            work_assistant_task_t *task = find_task_by_guid(s_detail_task_guid);
            if (task) {
                set_task_locally_completed(task->guid,
                                           !task_is_locally_completed(task->guid));
            }
            build_task_detail_screen();
        } else {
            s_task_detail_visible = false;
            s_detail_task_guid[0] = '\0';
            build_home_screen();
            refresh_battery();
        }
        return;
    }
    if (s_settings_visible) {
        if (btn == BSP_BTN_UP) {
            s_settings_selection =
                (s_settings_selection + WA_SETTINGS_ITEM_COUNT - 1U) %
                WA_SETTINGS_ITEM_COUNT;
            refresh_settings_selection();
            return;
        }
        if (btn == BSP_BTN_DOWN) {
            s_settings_selection =
                (s_settings_selection + 1U) % WA_SETTINGS_ITEM_COUNT;
            refresh_settings_selection();
            return;
        }
        if (btn != BSP_BTN_OK) return;

        if (s_settings_selection == 0) {
            s_auto_off_setting = work_assistant_auto_off_next(s_auto_off_setting);
            save_settings();
            refresh_settings_values();
        } else if (s_settings_selection == 1) {
            s_night_mode = !s_night_mode;
            save_settings();
            rebuild_current_screen();
        } else {
            refresh_settings_values();
        }
        return;
    }

    if (btn == BSP_BTN_OK) {
        if (s_home_selection == WA_HOME_SELECTION_AVATAR) {
            s_settings_visible = true;
            s_settings_selection = 0;
            rebuild_current_screen();
        } else if (s_home_selection == WA_HOME_SELECTION_SUMMARY) {
            s_tasks_visible = !s_tasks_visible;
            s_home_selection = WA_HOME_SELECTION_SUMMARY;
            if (s_list_title) {
                lv_label_set_text(s_list_title,
                                  s_tasks_visible ? "近 30 天待办" : "今日日程");
            }
            refresh_rows();
        } else if (s_home_selection == WA_HOME_SELECTION_ITEM && s_tasks_visible &&
                   s_selected_item < s_summary.task_count) {
            strlcpy(s_detail_task_guid, s_summary.tasks[s_selected_item].guid,
                    sizeof(s_detail_task_guid));
            s_task_detail_visible = true;
            s_task_detail_action = WA_TASK_DETAIL_ACTION_COMPLETE;
            build_task_detail_screen();
        }
        return;
    }

    size_t count = active_item_count();
    if (btn == BSP_BTN_UP) {
        if (s_home_selection == WA_HOME_SELECTION_SUMMARY) {
            s_home_selection = WA_HOME_SELECTION_AVATAR;
        } else if (s_home_selection == WA_HOME_SELECTION_ITEM) {
            if (s_selected_item == 0) {
                s_home_selection = WA_HOME_SELECTION_SUMMARY;
            } else {
                s_selected_item--;
            }
        }
    } else if (btn == BSP_BTN_DOWN) {
        if (s_home_selection == WA_HOME_SELECTION_AVATAR) {
            s_home_selection = WA_HOME_SELECTION_SUMMARY;
        } else if (s_home_selection == WA_HOME_SELECTION_SUMMARY && count > 0) {
            s_home_selection = WA_HOME_SELECTION_ITEM;
            s_selected_item = 0;
        } else if (s_home_selection == WA_HOME_SELECTION_ITEM &&
                   s_selected_item + 1 < count) {
            s_selected_item++;
        }
    }
    refresh_rows();
}
