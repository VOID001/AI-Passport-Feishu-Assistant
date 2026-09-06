#include "passport_ble_bridge.h"
#include "demo_radio.h"
#include "passport_ble_protocol.h"

#include "esp_log.h"
#include "mbedtls/base64.h"
#include "nvs.h"

#include <stdio.h>
#include <string.h>

#define SUMMARY_NAMESPACE "wa_bridge"
#define SUMMARY_KEY "summary"
#define QEMU_DEMO_DATE_KEY 20260905U
#define QEMU_DEMO_EPOCH 1788595200U
#define QEMU_DEMO_JSON_BYTES 4096U

static const char *TAG = "qemu_ble";
static passport_ble_bridge_state_t s_state = PASSPORT_BLE_BRIDGE_IDLE;
static work_assistant_summary_t s_summary;
static uint32_t s_revision;
static char s_summary_json[QEMU_DEMO_JSON_BYTES];
static const char QEMU_DEMO_AVATAR_RGB565[] =
    "///////////////////////////f/9//nfez3PHrLauCIL//3////////////////////////////////////////////////////////////9//q2LMqvDr8ePw49DrTstrqhHkvv//////////////////////////////////////////////////////vv/01Kx60XNTfDJ8M3yya02b7ZqGWdT0fvee9///////////////////////////////////3v/e/97/3v+d91Xd6GGTlPWU8Xtva25rT2vqQTOUE72XxZa9NqVd777/3v/e/97/3v/e///////////////f/zr/Ov86/zr/N94QlOxqLWPQe/CD8INylK+DjFLtYudBMpSXvdFzcpQ37vr+G/86/zr/Ov/e////////////3v86/xr/Gv833lS99bRve+xiTnvwixGMOc6Pg2tSZjnjGHOcs6zwgw5b9LS4/hv/Gv8b/xr/3v///////////9//G/8a/xr/UJzLYtSsLnNvg0+T8pNSlPSkq1opWst6THPwk6+Dk5zRe7OsuP4a/xr/Gv8b/97////////////f/xr/Gv8a/1CcSVIRpOlhSVqLWkpSSlLsekx79+UX7vbdRjHMYjKU0HsQlLn+Gv8a/xv/G//f////////////3v8a/xr/G/8wnIpacswtw0qSinpsg42DUdy5/tj+2P7V3WIoJTHtYlGUM736/hr/G/8b/xv/3////////////97/Gv8a//r+VM0PpNTMUuTQ85PkePa4/pX9FP3Z/rj+8+xwrI2Lh0lyxNn+Gv8b/xv/G/8a/97////////////f/xr/Gv/Z/nf2Fu6SrG+rkOOyxJO9k722zVT12P539kmCzpMQpA2bktTZ/hv/G/8b/xv/Gv/e////////////3v8a/xr/V+aU1bj+zpsKim/b7ptyrTO+++aX9tj+uPYwpfCcr6NS5BbuGv8b/xv/G/8b/xv/3////////////97/Gv8a//r2ue4a/xbmUdyQ67DbmeZ8977/2fbZ/vn+nO8b59XEc+yY/hv/G/8b/xv/G/8b/97////////////e/xr/Gv8a/zj2V+W5zVTckOtx65LkNv75/tn+mPbGWdj+9/U3zdHLuP4a/zr/Gv8b/xv/G//e////////////3/8b/xv/G/+a7rnNGOYy1DTk0+ty3Hf2uPb4/tj+Nu6Y9rfF+N0vs7j+Gv8a/xr/Gv8b/xv/3////////////97/Gv8a/xv/mu4Y3hjeNMwy41Lj5TjV1dj++P7Z/hr/d/Y7977/G/+Y/hr/Gv8b/xv/G/8b/97////////////e/xr/Gv8b/xv/ff+e//fsMePPqhe9VrXpQXe1mL35xVf2F95RlBjWUay4/hr/G/8b/xv/Gv/e////////////3v8a/xv/ON7Wxb7/vv/37K/C5UBSlAUpRTElMadBGs7YzZf+O/9d77n2uP4a/xr/G/8b/xv/3v///////////97/Gf/69r7/vv++/77/1tywuslhhkEJSgUp6ElJUtnFGdZ3/rruMYxsg9n+G/8b/zv/G/8b/97///////////+d95XF1sWe/77/nv+e/9Ss0rsqaotiV7W5xfWsOtaynF3/eO50zWyD2f46/zv/O/86/zr/Ov/f////////////HOeOe53/PPcc7z33/O7b5jzvz4N63nreO9ZTlL7/z4Oe/zz3d/47/zv/O/87/zv/O/87/zv/3////////////77/Xfe75tjNVbVWtRnOl8V63n3/ff/87na9yEF99/vmvv9d/1juGv9b/1v/O/9b/1v/e/9b/9////////////899z33GdbYzXXFL5y67lne2+5e/z33uMXLYrfFu959937/Xfd65rj+e/97/3v/e/97/3v/e//////////f/3Kke949/z33Pf/b9pj2G//4zXrmPf9d/3re18099/CLu+Zd937/PfeZ9hr/e/97/3z/fP98/3z/////////3v8kWXK8Pf88/xr/+vZUvRS93O49/z3/Pfc99z3/PffoUS5zHPcY1l33GNbytNK0dMVb/5z/nP+c/////////97/x3kNq3re+v7Z/tn+kawQlHrm/fYd9x33Hfcd9z3/Nb2TpBz3Wt5+/7vmFLXPk/K09tV35nv/nP/////////e/2myaqoxxNj+2P7Y/jbm1N343ZzmHfcd9x33Hfcd9/z2DGsd913/Xf+75u+DiXrOu427jsM37tn+3v//////3v/J0uvKVM34/tn+2P7Z/pf+t9Vb5v32HPf99hz3EZQQlElaEYw9/xjWSVKGUUmq68qqumqylOW4/r7/////////uvbU7ZG8rZvZ/tj+2f6X/hCklKSb5hGUr4uvi8xq62qqamY5Gdbxk/CTYSimgcrSqsJqso2jNu7e/////////97/k+XnaTbu2f7Y/tn+l/6Y7rjFj4OKYklay2rMcutyy2pqWk5zmMXTrCAQRGmqyorCqsKKsoZZ++b/////4xiuuxLtCouU7TDMb6yNk7HU8tyqashBimKrautqzHLLastqTHszzdCDOebregRZibpEgeeZa7Ly3J3//////yAQwjhLs5T1sdQLmy+cS4Nx1LHc+vaug6ti62rrcutyy2rLaqpi50mnOZOsDJOCMMaJqtKJwmmyc+2+//////8=";

static bool seed_demo_summary(void)
{
    s_summary = (work_assistant_summary_t) {
        .account_name = "演示用户",
        .today_key = QEMU_DEMO_DATE_KEY,
        .generated_at_epoch = QEMU_DEMO_EPOCH,
        .utc_offset_minutes = 480,
        .overdue_task_count = 1,
        .unread_message_count = 999,
        .avatar_valid = true,
        .event_count = 5,
        .events = {
            {
                .day_key = QEMU_DEMO_DATE_KEY,
                .start_minute = 9 * 60 + 8,
                .end_minute = 10 * 60 + 30,
                .title = "演示 FoloToy AI Passport",
            },
            {
                .day_key = QEMU_DEMO_DATE_KEY,
                .start_minute = 12 * 60 + 3,
                .end_minute = 13 * 60,
                .title = "飞书日程同步功能开发",
            },
            {
                .day_key = QEMU_DEMO_DATE_KEY,
                .start_minute = 0,
                .end_minute = 24 * 60,
                .all_day = true,
                .title = "夜间模式开发",
            },
            {
                .day_key = QEMU_DEMO_DATE_KEY,
                .start_minute = 9 * 60,
                .end_minute = 10 * 60,
                .title = "同步功能开发",
            },
            {
                .day_key = QEMU_DEMO_DATE_KEY,
                .start_minute = 0,
                .end_minute = 24 * 60,
                .all_day = true,
                .title = "FoloToy AI Passport - Feishu Assistant",
            },
        },
        .task_count = 1,
        .tasks = {
            {
                .guid = "qemu-demo-confirm-content",
                .due_at_epoch = 1788580800U,
                .title = "确认演示内容",
            },
        },
    };
    size_t avatar_length = 0;
    if (mbedtls_base64_decode(
            s_summary.avatar_rgb565, sizeof(s_summary.avatar_rgb565),
            &avatar_length, (const unsigned char *)QEMU_DEMO_AVATAR_RGB565,
            strlen(QEMU_DEMO_AVATAR_RGB565)) != 0 ||
        avatar_length != sizeof(s_summary.avatar_rgb565)) {
        return false;
    }

    int length = snprintf(
        s_summary_json, sizeof(s_summary_json),
        "{\"version\":4,\"generated_at\":\"2026-09-05T08:00:00Z\","
        "\"generated_at_epoch\":1788595200,\"utc_offset_minutes\":480,"
        "\"user_name\":\"演示用户\",\"avatar_rgb565\":\"%s\","
        "\"date\":\"2026-09-05\",\"events\":["
        "{\"title\":\"演示 FoloToy AI Passport\",\"start_minute\":548,"
        "\"end_minute\":630,\"all_day\":false,\"completed\":false},"
        "{\"title\":\"飞书日程同步功能开发\",\"start_minute\":723,"
        "\"end_minute\":780,\"all_day\":false,\"completed\":false},"
        "{\"title\":\"夜间模式开发\",\"start_minute\":0,\"end_minute\":1440,"
        "\"all_day\":true,\"completed\":false},"
        "{\"title\":\"同步功能开发\",\"start_minute\":540,\"end_minute\":600,"
        "\"all_day\":false,\"completed\":false},"
        "{\"title\":\"FoloToy AI Passport - Feishu Assistant\","
        "\"start_minute\":0,\"end_minute\":1440,\"all_day\":true,"
        "\"completed\":false}],\"overdue_task_count\":1,"
        "\"unread_message_count\":999,\"tasks\":["
        "{\"guid\":\"qemu-demo-confirm-content\",\"title\":\"确认演示内容\","
        "\"due_at_epoch\":1788580800,\"all_day\":false}]}",
        QEMU_DEMO_AVATAR_RGB565);
    if (length < 0 || (size_t)length >= sizeof(s_summary_json) ||
        (size_t)length > PASSPORT_BLE_PAYLOAD_MAX) {
        return false;
    }

    nvs_handle_t handle;
    if (nvs_open(SUMMARY_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_set_blob(handle, SUMMARY_KEY, s_summary_json,
                                 (size_t)length + 1);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

bool passport_ble_bridge_start(void)
{
    if (s_state == PASSPORT_BLE_BRIDGE_READY) return true;
    if (demo_radio_nvs_prepare() != ESP_OK || !seed_demo_summary()) {
        s_state = PASSPORT_BLE_BRIDGE_ERROR;
        ESP_LOGE(TAG, "Unable to seed QEMU demo NVS summary");
        return false;
    }
    s_revision++;
    ESP_LOGI(TAG, "Loaded QEMU demo NVS summary (%u bytes)",
             (unsigned)strlen(s_summary_json));
    s_state = PASSPORT_BLE_BRIDGE_READY;
    return true;
}

bool passport_ble_bridge_stop(void)
{
    s_state = PASSPORT_BLE_BRIDGE_IDLE;
    return true;
}

passport_ble_bridge_state_t passport_ble_bridge_state(void)
{
    return s_state;
}

const char *passport_ble_bridge_status_message(void)
{
    return s_state == PASSPORT_BLE_BRIDGE_READY
               ? "QEMU demo data loaded. No radio hardware."
               : "";
}

bool passport_ble_bridge_pairing_code(uint32_t *code)
{
    (void)code;
    return false;
}

uint32_t passport_ble_bridge_revision(void)
{
    return s_revision;
}

bool passport_ble_bridge_copy_summary(work_assistant_summary_t *summary)
{
    if (!summary || s_state != PASSPORT_BLE_BRIDGE_READY) return false;
    *summary = s_summary;
    return true;
}

bool passport_ble_bridge_serial_number(char out[13])
{
    if (!out) return false;
    memcpy(out, "QEMU00000001", 13);
    return true;
}

bool passport_ble_bridge_enqueue_completion(const char *guid)
{
    return guid && guid[0];
}

uint8_t passport_ble_bridge_protocol_version(void)
{
    return PASSPORT_BLE_PROTOCOL_VERSION;
}
