#include "passport_ble_bridge.h"

#include "cJSON.h"
#include "demo_radio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/util/util.h"
#include "esp_random.h"
#include "mbedtls/base64.h"
#include "nimble/nimble_port.h"
#include "nvs.h"
#include "os/os_mbuf.h"
#include "passport_ble_protocol.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#define SUMMARY_NAMESPACE "wa_bridge"
#define SUMMARY_KEY "summary"
#define COMPLETIONS_KEY "completions"
#define FRAME_MAX 512

static const char *TAG = "passport_ble";
static ble_uuid128_t s_service_uuid =
    BLE_UUID128_INIT(0x3e, 0xe9, 0xcf, 0x6e, 0xad, 0x92, 0x9d, 0xbd,
                     0x5a, 0x48, 0xbd, 0xf7, 0x8a, 0xa2, 0x2e, 0x7d);
static ble_uuid128_t s_write_uuid =
    BLE_UUID128_INIT(0x3e, 0xe9, 0xcf, 0x6e, 0xad, 0x92, 0x9d, 0xbd,
                     0x5a, 0x48, 0xbd, 0xf7, 0x8b, 0xa2, 0x2e, 0x7d);
static ble_uuid128_t s_status_uuid =
    BLE_UUID128_INIT(0x3e, 0xe9, 0xcf, 0x6e, 0xad, 0x92, 0x9d, 0xbd,
                     0x5a, 0x48, 0xbd, 0xf7, 0x8c, 0xa2, 0x2e, 0x7d);
static ble_uuid128_t s_completions_uuid =
    BLE_UUID128_INIT(0x3e, 0xe9, 0xcf, 0x6e, 0xad, 0x92, 0x9d, 0xbd,
                     0x5a, 0x48, 0xbd, 0xf7, 0x8d, 0xa2, 0x2e, 0x7d);
static ble_uuid128_t s_completions_ack_uuid =
    BLE_UUID128_INIT(0x3e, 0xe9, 0xcf, 0x6e, 0xad, 0x92, 0x9d, 0xbd,
                     0x5a, 0x48, 0xbd, 0xf7, 0x8e, 0xa2, 0x2e, 0x7d);
static ble_uuid128_t s_protocol_uuid =
    BLE_UUID128_INIT(0x3e, 0xe9, 0xcf, 0x6e, 0xad, 0x92, 0x9d, 0xbd,
                     0x5a, 0x48, 0xbd, 0xf7, 0x8f, 0xa2, 0x2e, 0x7d);

typedef struct {
    char *payload;
    size_t length;
} summary_message_t;

static volatile passport_ble_bridge_state_t s_state;
static volatile bool s_stop_requested;
static bool s_initialized;
static bool s_host_running;
static bool s_has_summary;
static volatile uint32_t s_summary_revision;
static uint8_t s_addr_type;
static uint8_t s_serial_address[6];
static bool s_serial_address_valid;
static uint8_t s_last_result;
static uint16_t s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_status_value_handle;
static uint16_t s_completions_value_handle;
static uint16_t s_completions_ack_value_handle;
static uint16_t s_protocol_value_handle;
static volatile uint32_t s_pairing_code;
static TaskHandle_t s_worker_task;
static QueueHandle_t s_summary_queue;
static SemaphoreHandle_t s_summary_mutex;
static SemaphoreHandle_t s_host_stopped;
static SemaphoreHandle_t s_worker_stopped;
static passport_ble_receiver_t *s_receiver;
static work_assistant_summary_t *s_summary;
static char s_completions[WORK_ASSISTANT_MAX_TASKS][WORK_ASSISTANT_TASK_GUID_MAX];
static size_t s_completion_count;

static bool save_completions(void)
{
    nvs_handle_t handle;
    if (nvs_open(SUMMARY_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(handle, COMPLETIONS_KEY, s_completions,
                                 sizeof(s_completions));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

static void load_completions(void)
{
    memset(s_completions, 0, sizeof(s_completions));
    s_completion_count = 0;
    nvs_handle_t handle;
    size_t size = sizeof(s_completions);
    if (nvs_open(SUMMARY_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    if (nvs_get_blob(handle, COMPLETIONS_KEY, s_completions, &size) == ESP_OK &&
        size == sizeof(s_completions)) {
        while (s_completion_count < WORK_ASSISTANT_MAX_TASKS &&
               s_completions[s_completion_count][0]) s_completion_count++;
    }
    nvs_close(handle);
}

void ble_store_config_init(void);

static int gap_event(struct ble_gap_event *event, void *arg);

static void copy_utf8(char *target, size_t capacity, const char *source)
{
    if (!target || capacity == 0) return;
    if (!source) {
        target[0] = '\0';
        return;
    }
    size_t length = strlen(source);
    if (length >= capacity) {
        length = capacity - 1;
        while (length > 0 && ((unsigned char)source[length] & 0xC0U) == 0x80U) {
            length--;
        }
    }
    memcpy(target, source, length);
    target[length] = '\0';
}

static bool object_has_only(const cJSON *object, const char *const *allowed,
                            size_t allowed_count)
{
    cJSON *field = NULL;
    cJSON_ArrayForEach(field, object) {
        bool found = false;
        for (size_t index = 0; index < allowed_count; index++) {
            if (!strcmp(field->string, allowed[index])) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

static bool integer_between(const cJSON *item, int64_t minimum, int64_t maximum)
{
    if (!cJSON_IsNumber(item) || item->valuedouble < (double)minimum ||
        item->valuedouble > (double)maximum) {
        return false;
    }
    return (double)(int64_t)item->valuedouble == item->valuedouble;
}

static bool parse_date_key(const char *date, uint32_t *value)
{
    unsigned year;
    unsigned month;
    unsigned day;
    char tail;
    if (!date || sscanf(date, "%4u-%2u-%2u%c", &year, &month, &day, &tail) != 3 ||
        year < 2000 || month < 1 || month > 12 || day < 1 || day > 31) {
        return false;
    }
    *value = year * 10000U + month * 100U + day;
    return true;
}

static bool parse_summary(const char *json, work_assistant_summary_t *summary)
{
    static const char *const summary_fields[] = {
        "version", "generated_at", "generated_at_epoch", "utc_offset_minutes",
        "user_name", "avatar_rgb565", "date", "events", "overdue_task_count",
        "unread_message_count", "tasks",
    };
    static const char *const event_fields[] = {
        "title", "start_minute", "end_minute", "all_day", "completed",
    };
    static const char *const task_fields[] = {
        "guid", "title", "due_at_epoch", "all_day",
    };

    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root) ||
        !object_has_only(root, summary_fields,
                         sizeof(summary_fields) / sizeof(summary_fields[0]))) {
        cJSON_Delete(root);
        return false;
    }
    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    cJSON *generated_at = cJSON_GetObjectItemCaseSensitive(root, "generated_at");
    cJSON *generated_at_epoch =
        cJSON_GetObjectItemCaseSensitive(root, "generated_at_epoch");
    cJSON *utc_offset =
        cJSON_GetObjectItemCaseSensitive(root, "utc_offset_minutes");
    cJSON *user_name = cJSON_GetObjectItemCaseSensitive(root, "user_name");
    cJSON *avatar = cJSON_GetObjectItemCaseSensitive(root, "avatar_rgb565");
    cJSON *date = cJSON_GetObjectItemCaseSensitive(root, "date");
    cJSON *events = cJSON_GetObjectItemCaseSensitive(root, "events");
    cJSON *tasks = cJSON_GetObjectItemCaseSensitive(root, "tasks");
    cJSON *overdue = cJSON_GetObjectItemCaseSensitive(root, "overdue_task_count");
    cJSON *unread = cJSON_GetObjectItemCaseSensitive(root, "unread_message_count");
    if (!integer_between(version, PASSPORT_BLE_PROTOCOL_VERSION,
                         PASSPORT_BLE_PROTOCOL_VERSION) ||
        !cJSON_IsString(generated_at) || !generated_at->valuestring[0] ||
        !integer_between(generated_at_epoch, 0, UINT32_MAX) ||
        !integer_between(utc_offset, -840, 840) ||
        !cJSON_IsString(user_name) || !user_name->valuestring[0] ||
        !cJSON_IsString(avatar) ||
        !cJSON_IsString(date) || !cJSON_IsArray(events) || !cJSON_IsArray(tasks) ||
        cJSON_GetArraySize(events) > WORK_ASSISTANT_MAX_EVENTS ||
        cJSON_GetArraySize(tasks) > WORK_ASSISTANT_MAX_TASKS ||
        !integer_between(overdue, 0, UINT16_MAX) ||
        !integer_between(unread, 0, UINT16_MAX)) {
        cJSON_Delete(root);
        return false;
    }

    *summary = (work_assistant_summary_t) { 0 };
    if (!parse_date_key(date->valuestring, &summary->today_key)) {
        cJSON_Delete(root);
        return false;
    }
    summary->generated_at_epoch = (uint32_t)generated_at_epoch->valuedouble;
    summary->utc_offset_minutes = (int16_t)utc_offset->valueint;
    copy_utf8(summary->account_name, sizeof(summary->account_name),
              user_name->valuestring);
    summary->overdue_task_count = (uint16_t)overdue->valueint;
    summary->unread_message_count = (uint16_t)unread->valueint;
    if (avatar->valuestring[0]) {
        size_t avatar_length = 0;
        int result = mbedtls_base64_decode(
            summary->avatar_rgb565, sizeof(summary->avatar_rgb565),
            &avatar_length, (const unsigned char *)avatar->valuestring,
            strlen(avatar->valuestring));
        if (result != 0 || avatar_length != sizeof(summary->avatar_rgb565)) {
            cJSON_Delete(root);
            return false;
        }
        summary->avatar_valid = true;
    }

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, events) {
        if (!cJSON_IsObject(item) ||
            !object_has_only(item, event_fields,
                             sizeof(event_fields) / sizeof(event_fields[0]))) {
            cJSON_Delete(root);
            return false;
        }
        cJSON *title = cJSON_GetObjectItemCaseSensitive(item, "title");
        cJSON *start = cJSON_GetObjectItemCaseSensitive(item, "start_minute");
        cJSON *end = cJSON_GetObjectItemCaseSensitive(item, "end_minute");
        cJSON *all_day = cJSON_GetObjectItemCaseSensitive(item, "all_day");
        cJSON *completed = cJSON_GetObjectItemCaseSensitive(item, "completed");
        if (!cJSON_IsString(title) || !title->valuestring[0] ||
            strlen(title->valuestring) >= WORK_ASSISTANT_TITLE_MAX ||
            !integer_between(start, 0, 1440) ||
            !integer_between(end, start->valueint, 1440) ||
            !cJSON_IsBool(all_day) ||
            !cJSON_IsBool(completed)) {
            cJSON_Delete(root);
            return false;
        }
        work_assistant_event_t *event =
            &summary->events[summary->event_count++];
        event->day_key = summary->today_key;
        event->start_minute = (uint16_t)start->valueint;
        event->end_minute = (uint16_t)end->valueint;
        event->all_day = cJSON_IsTrue(all_day);
        event->completed = cJSON_IsTrue(completed);
        copy_utf8(event->title, sizeof(event->title), title->valuestring);
    }

    cJSON_ArrayForEach(item, tasks) {
        if (!cJSON_IsObject(item) ||
            !object_has_only(item, task_fields,
                             sizeof(task_fields) / sizeof(task_fields[0]))) {
            cJSON_Delete(root);
            return false;
        }
        cJSON *guid = cJSON_GetObjectItemCaseSensitive(item, "guid");
        cJSON *title = cJSON_GetObjectItemCaseSensitive(item, "title");
        cJSON *due_at = cJSON_GetObjectItemCaseSensitive(item, "due_at_epoch");
        cJSON *all_day = cJSON_GetObjectItemCaseSensitive(item, "all_day");
        if (!cJSON_IsString(guid) || !guid->valuestring[0] ||
            strlen(guid->valuestring) >= WORK_ASSISTANT_TASK_GUID_MAX ||
            !cJSON_IsString(title) || !title->valuestring[0] ||
            strlen(title->valuestring) >= WORK_ASSISTANT_TASK_TITLE_MAX ||
            !integer_between(due_at, 0, UINT32_MAX) || !cJSON_IsBool(all_day)) {
            cJSON_Delete(root);
            return false;
        }
        work_assistant_task_t *task = &summary->tasks[summary->task_count++];
        copy_utf8(task->guid, sizeof(task->guid), guid->valuestring);
        task->due_at_epoch = (uint32_t)due_at->valuedouble;
        task->all_day = cJSON_IsTrue(all_day);
        copy_utf8(task->title, sizeof(task->title), title->valuestring);
    }
    cJSON_Delete(root);
    return true;
}

static bool save_summary(const char *json, size_t length)
{
    nvs_handle_t handle;
    if (nvs_open(SUMMARY_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(handle, SUMMARY_KEY, json, length + 1);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

static void apply_summary_time(const work_assistant_summary_t *summary)
{
    if (!summary || !summary->generated_at_epoch) return;
    struct timeval value = {
        .tv_sec = (time_t)summary->generated_at_epoch,
        .tv_usec = 0,
    };
    settimeofday(&value, NULL);
}

static bool load_summary(void)
{
    nvs_handle_t handle;
    if (nvs_open(SUMMARY_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
    size_t length = 0;
    esp_err_t err = nvs_get_blob(handle, SUMMARY_KEY, NULL, &length);
    if (err != ESP_OK || length == 0 || length > PASSPORT_BLE_PAYLOAD_MAX + 1) {
        nvs_close(handle);
        return false;
    }
    char *json = malloc(length);
    if (!json) {
        nvs_close(handle);
        return false;
    }
    err = nvs_get_blob(handle, SUMMARY_KEY, json, &length);
    nvs_close(handle);
    work_assistant_summary_t *parsed = malloc(sizeof(*parsed));
    bool valid = parsed && err == ESP_OK && json[length - 1] == '\0' &&
                 parse_summary(json, parsed);
    free(json);
    if (!valid) {
        free(parsed);
        return false;
    }
    if (xSemaphoreTake(s_summary_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        free(parsed);
        return false;
    }
    free(s_summary);
    s_summary = parsed;
    s_has_summary = true;
    s_summary_revision++;
    xSemaphoreGive(s_summary_mutex);
    apply_summary_time(parsed);
    return true;
}

static void notify_status(uint16_t connection_handle)
{
    if (connection_handle == BLE_HS_CONN_HANDLE_NONE || !s_status_value_handle) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&s_last_result, sizeof(s_last_result));
    if (om) ble_gatts_notify_custom(connection_handle, s_status_value_handle, om);
}

static int gatt_access(uint16_t connection_handle, uint16_t attribute_handle,
                       struct ble_gatt_access_ctxt *context, void *arg)
{
    (void)arg;
    if (attribute_handle == s_completions_value_handle &&
        context->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        char payload[WORK_ASSISTANT_MAX_TASKS * WORK_ASSISTANT_TASK_GUID_MAX] = { 0 };
        for (size_t index = 0; index < s_completion_count; index++) {
            strlcat(payload, s_completions[index], sizeof(payload));
            strlcat(payload, "\n", sizeof(payload));
        }
        return os_mbuf_append(context->om, payload, strlen(payload)) == 0
                   ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (attribute_handle == s_status_value_handle &&
        context->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return os_mbuf_append(context->om, &s_last_result, sizeof(s_last_result)) == 0
                   ? 0
                   : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (attribute_handle == s_protocol_value_handle &&
        context->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const uint8_t version = PASSPORT_BLE_PROTOCOL_VERSION;
        return os_mbuf_append(context->om, &version, sizeof(version)) == 0
                   ? 0
                   : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;

    uint16_t frame_length = OS_MBUF_PKTLEN(context->om);
    if (frame_length == 0 || frame_length > FRAME_MAX) {
        s_last_result = PASSPORT_BLE_FRAME_BAD_FORMAT;
        notify_status(connection_handle);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    uint8_t frame[FRAME_MAX];
    uint16_t copied = 0;
    if (ble_hs_mbuf_to_flat(context->om, frame, sizeof(frame), &copied) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (attribute_handle == s_completions_ack_value_handle &&
        copied < WORK_ASSISTANT_TASK_GUID_MAX) {
        frame[copied] = '\0';
        for (size_t index = 0; index < s_completion_count; index++) {
            if (!strcmp((char *)frame, s_completions[index])) {
                memmove(&s_completions[index], &s_completions[index + 1],
                        (s_completion_count - index - 1) *
                        sizeof(s_completions[0]));
                s_completion_count--;
                memset(s_completions[s_completion_count], 0,
                       sizeof(s_completions[0]));
                save_completions();
                return 0;
            }
        }
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (!s_receiver) return BLE_ATT_ERR_UNLIKELY;
    passport_ble_frame_result_t result =
        passport_ble_receiver_write(s_receiver, frame, copied);
    s_last_result = (uint8_t)result;
    if (result == PASSPORT_BLE_FRAME_ACCEPTED) {
        s_state = PASSPORT_BLE_BRIDGE_RECEIVING;
    } else if (result == PASSPORT_BLE_FRAME_COMPLETE) {
        s_last_result = PASSPORT_BLE_FRAME_ACCEPTED;
        summary_message_t message = { 0 };
        message.length = s_receiver->received_length;
        message.payload = malloc(message.length + 1);
        if (!message.payload) {
            s_last_result = PASSPORT_BLE_FRAME_BAD_STATE;
        } else {
            memcpy(message.payload, s_receiver->payload, message.length + 1);
            if (xQueueSend(s_summary_queue, &message, 0) != pdTRUE) {
                free(message.payload);
                s_last_result = PASSPORT_BLE_FRAME_BAD_STATE;
            }
        }
    }
    notify_status(connection_handle);
    return 0;
}

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_write_uuid.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP |
                         BLE_GATT_CHR_F_WRITE_AUTHEN,
            },
            {
                .uuid = &s_status_uuid.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_AUTHEN |
                         BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_status_value_handle,
            },
            {
                .uuid = &s_completions_uuid.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_AUTHEN,
                .val_handle = &s_completions_value_handle,
            },
            {
                .uuid = &s_completions_ack_uuid.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_AUTHEN,
                .val_handle = &s_completions_ack_value_handle,
            },
            {
                .uuid = &s_protocol_uuid.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_AUTHEN,
                .val_handle = &s_protocol_value_handle,
            },
            { 0 },
        },
    },
    { 0 },
};

static int advertise(void)
{
    struct ble_hs_adv_fields fields = { 0 };
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = &s_service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) return rc;

    struct ble_hs_adv_fields response = { 0 };
    response.name = (uint8_t *)PASSPORT_BLE_DEVICE_NAME;
    response.name_len = strlen(PASSPORT_BLE_DEVICE_NAME);
    response.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&response);
    if (rc != 0) return rc;

    struct ble_gap_adv_params parameters = { 0 };
    parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
    parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER,
                           &parameters, gap_event, NULL);
    if (rc == 0) {
        s_state = s_has_summary ? PASSPORT_BLE_BRIDGE_READY
                                : PASSPORT_BLE_BRIDGE_ADVERTISING;
    }
    return rc;
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_connection_handle = event->connect.conn_handle;
            s_state = PASSPORT_BLE_BRIDGE_CONNECTED;
            ble_gap_security_initiate(s_connection_handle);
        } else {
            advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
        s_pairing_code = 0;
        advertise();
        break;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.params.action != BLE_SM_IOACT_DISP) break;
        {
            struct ble_sm_io pairing = {
                .action = BLE_SM_IOACT_DISP,
                .passkey = 100000U + (esp_random() % 900000U),
            };
            s_pairing_code = pairing.passkey;
            s_state = PASSPORT_BLE_BRIDGE_PAIRING;
            int rc = ble_sm_inject_io(event->passkey.conn_handle, &pairing);
            if (rc != 0) {
                ESP_LOGE(TAG, "BLE pairing code setup failed: %d", rc);
                s_pairing_code = 0;
                s_state = PASSPORT_BLE_BRIDGE_ERROR;
                return rc;
            }
        }
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0) {
            s_pairing_code = 0;
            s_state = PASSPORT_BLE_BRIDGE_CONNECTED;
        }
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        {
            struct ble_gap_conn_desc description;
            int rc = ble_gap_conn_find(event->repeat_pairing.conn_handle,
                                       &description);
            if (rc != 0) return rc;
            ble_store_util_delete_peer(&description.peer_id_addr);
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (!s_stop_requested) advertise();
        break;
    default:
        break;
    }
    return 0;
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE reset: %d", reason);
    s_state = PASSPORT_BLE_BRIDGE_ERROR;
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc == 0) {
        rc = ble_hs_id_copy_addr(s_addr_type, s_serial_address, NULL);
        s_serial_address_valid = rc == 0;
    }
    if (rc == 0) rc = advertise();
    if (rc != 0) {
        ESP_LOGE(TAG, "BLE advertising failed: %d", rc);
        s_state = PASSPORT_BLE_BRIDGE_ERROR;
    }
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    s_host_running = false;
    if (s_host_stopped) xSemaphoreGive(s_host_stopped);
    vTaskDelete(NULL);
}

static bool start_nimble(void)
{
    if (nimble_port_init() != ESP_OK) return false;
    s_initialized = true;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (ble_svc_gap_device_name_set(PASSPORT_BLE_DEVICE_NAME) != 0 ||
        ble_gatts_count_cfg(s_services) != 0 ||
        ble_gatts_add_svcs(s_services) != 0) {
        esp_err_t err = nimble_port_deinit();
        if (err == ESP_OK) {
            s_initialized = false;
        } else {
            ESP_LOGE(TAG, "BLE rollback failed: %s", esp_err_to_name(err));
        }
        return false;
    }
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_store_config_init();
    s_host_running = true;
    BaseType_t task_result = xTaskCreatePinnedToCore(
        host_task, "nimble_host", NIMBLE_HS_STACK_SIZE, NULL,
        configMAX_PRIORITIES - 4, NULL, NIMBLE_CORE);
    if (task_result != pdPASS) {
        s_host_running = false;
        esp_err_t err = nimble_port_deinit();
        if (err == ESP_OK) {
            s_initialized = false;
        } else {
            ESP_LOGE(TAG, "BLE host allocation rollback failed: %s",
                     esp_err_to_name(err));
        }
        return false;
    }
    return true;
}

static void worker_task(void *arg)
{
    (void)arg;
    if (demo_radio_nvs_prepare() != ESP_OK) {
        s_state = PASSPORT_BLE_BRIDGE_ERROR;
        goto done;
    }
    load_summary();
    load_completions();
    if (s_stop_requested) goto done;
    if (!start_nimble()) {
        s_state = PASSPORT_BLE_BRIDGE_ERROR;
        goto done;
    }

    while (!s_stop_requested) {
        summary_message_t message;
        if (xQueueReceive(s_summary_queue, &message, pdMS_TO_TICKS(500)) != pdTRUE) {
            continue;
        }
        work_assistant_summary_t *parsed = malloc(sizeof(*parsed));
        bool parsed_ok = parsed && parse_summary(message.payload, parsed);
        bool saved = parsed_ok && save_summary(message.payload, message.length);
        if (saved &&
            xSemaphoreTake(s_summary_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
            free(s_summary);
            s_summary = parsed;
            parsed = NULL;
            s_has_summary = true;
            apply_summary_time(s_summary);
            s_summary_revision++;
            s_state = PASSPORT_BLE_BRIDGE_READY;
            xSemaphoreGive(s_summary_mutex);
            s_last_result = PASSPORT_BLE_FRAME_COMPLETE;
        } else {
            s_state = PASSPORT_BLE_BRIDGE_ERROR;
            s_last_result = parsed_ok ? PASSPORT_BLE_FRAME_BAD_STATE
                                      : PASSPORT_BLE_FRAME_BAD_FORMAT;
        }
        free(parsed);
        free(message.payload);
        notify_status(s_connection_handle);
    }

done:
    if (s_worker_stopped) xSemaphoreGive(s_worker_stopped);
    vTaskDelete(NULL);
}

static void clear_bridge_data(void)
{
    if (s_summary_queue) {
        summary_message_t message;
        while (xQueueReceive(s_summary_queue, &message, 0) == pdTRUE) {
            free(message.payload);
        }
    }
    free(s_receiver);
    s_receiver = NULL;
    if (s_summary_mutex &&
        xSemaphoreTake(s_summary_mutex, portMAX_DELAY) == pdTRUE) {
        s_has_summary = false;
        free(s_summary);
        s_summary = NULL;
        xSemaphoreGive(s_summary_mutex);
    }
}

bool passport_ble_bridge_start(void)
{
    if (s_worker_task) {
        return !s_stop_requested && s_state != PASSPORT_BLE_BRIDGE_ERROR;
    }
    if (s_initialized || s_receiver || s_summary) {
        ESP_LOGE(TAG, "BLE bridge has not fully stopped");
        s_state = PASSPORT_BLE_BRIDGE_ERROR;
        return false;
    }
    s_stop_requested = false;
    s_state = PASSPORT_BLE_BRIDGE_STARTING;
    s_pairing_code = 0;
    s_serial_address_valid = false;
    s_receiver = calloc(1, sizeof(*s_receiver));
    if (!s_receiver) {
        ESP_LOGE(TAG, "BLE receiver allocation failed");
        s_state = PASSPORT_BLE_BRIDGE_ERROR;
        return false;
    }
    passport_ble_receiver_init(s_receiver);
    s_last_result = PASSPORT_BLE_FRAME_ACCEPTED;
    if (!s_summary_mutex) s_summary_mutex = xSemaphoreCreateMutex();
    if (!s_summary_queue) s_summary_queue = xQueueCreate(1, sizeof(summary_message_t));
    if (!s_host_stopped) s_host_stopped = xSemaphoreCreateBinary();
    if (!s_worker_stopped) s_worker_stopped = xSemaphoreCreateBinary();
    if (!s_summary_mutex || !s_summary_queue || !s_host_stopped || !s_worker_stopped) {
        ESP_LOGE(TAG, "BLE bridge resource allocation failed");
        clear_bridge_data();
        s_state = PASSPORT_BLE_BRIDGE_ERROR;
        return false;
    }
    xSemaphoreTake(s_host_stopped, 0);
    xSemaphoreTake(s_worker_stopped, 0);
    /*
     * The worker reloads persistent state before NimBLE starts. Clear a prior
     * in-memory session now so a later launch cannot display stale data while
     * that background load is still pending.
     */
    s_has_summary = false;
    BaseType_t task_result = xTaskCreate(worker_task, "passport_ble", 6144, NULL, 4,
                                         &s_worker_task);
    if (task_result == pdPASS) {
        return true;
    }
    clear_bridge_data();
    s_state = PASSPORT_BLE_BRIDGE_ERROR;
    return false;
}

bool passport_ble_bridge_stop(void)
{
    s_stop_requested = true;
    s_state = PASSPORT_BLE_BRIDGE_IDLE;
    if (s_worker_task && s_worker_stopped &&
        xSemaphoreTake(s_worker_stopped, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "BLE worker task did not stop");
        s_state = PASSPORT_BLE_BRIDGE_ERROR;
        return false;
    }
    s_worker_task = NULL;
    if (s_initialized && s_host_running) {
        bool host_stopped =
            s_host_stopped && xSemaphoreTake(s_host_stopped, 0) == pdTRUE;
        if (!host_stopped) {
            ble_gap_adv_stop();
            int rc = nimble_port_stop();
            if (rc != 0) {
                ESP_LOGE(TAG, "BLE host stop failed: %d", rc);
                s_state = PASSPORT_BLE_BRIDGE_ERROR;
                return false;
            }
            if (s_host_stopped &&
                xSemaphoreTake(s_host_stopped, pdMS_TO_TICKS(3000)) != pdTRUE) {
                ESP_LOGE(TAG, "BLE host task did not stop");
                s_state = PASSPORT_BLE_BRIDGE_ERROR;
                return false;
            }
        }
    }
    s_host_running = false;
    if (s_initialized) {
        esp_err_t err = nimble_port_deinit();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "BLE deinitialization failed: %s",
                     esp_err_to_name(err));
            s_state = PASSPORT_BLE_BRIDGE_ERROR;
            return false;
        }
        s_initialized = false;
    }
    clear_bridge_data();
    s_pairing_code = 0;
    return true;
}

passport_ble_bridge_state_t passport_ble_bridge_state(void)
{
    return s_state;
}

const char *passport_ble_bridge_status_message(void)
{
    switch (s_state) {
    case PASSPORT_BLE_BRIDGE_STARTING:
        return "Starting Bluetooth.";
    case PASSPORT_BLE_BRIDGE_ADVERTISING:
        return "Waiting for computer.";
    case PASSPORT_BLE_BRIDGE_CONNECTED:
        return "Computer connected.";
    case PASSPORT_BLE_BRIDGE_PAIRING:
        return "Enter the pairing code on your computer.";
    case PASSPORT_BLE_BRIDGE_RECEIVING:
        return "Receiving work data.";
    case PASSPORT_BLE_BRIDGE_READY:
        return "Work data received.";
    case PASSPORT_BLE_BRIDGE_ERROR:
        return "Bluetooth sync failed.";
    default:
        return "";
    }
}

bool passport_ble_bridge_pairing_code(uint32_t *code)
{
    uint32_t pairing_code = s_pairing_code;
    if (!code || pairing_code < 100000U || pairing_code > 999999U) return false;
    *code = pairing_code;
    return true;
}

bool passport_ble_bridge_serial_number(char out[13])
{
    if (!out || !s_serial_address_valid) return false;
    snprintf(out, 13, "%02X%02X%02X%02X%02X%02X",
             s_serial_address[5], s_serial_address[4], s_serial_address[3],
             s_serial_address[2], s_serial_address[1], s_serial_address[0]);
    return true;
}

bool passport_ble_bridge_enqueue_completion(const char *guid)
{
    if (!guid || !guid[0] || strlen(guid) >= WORK_ASSISTANT_TASK_GUID_MAX) return false;
    for (size_t index = 0; index < s_completion_count; index++) {
        if (!strcmp(guid, s_completions[index])) return true;
    }
    if (s_completion_count >= WORK_ASSISTANT_MAX_TASKS) return false;
    strlcpy(s_completions[s_completion_count++], guid, WORK_ASSISTANT_TASK_GUID_MAX);
    if (save_completions()) return true;
    s_completion_count--;
    memset(s_completions[s_completion_count], 0, sizeof(s_completions[0]));
    return false;
}

uint8_t passport_ble_bridge_protocol_version(void)
{
    return PASSPORT_BLE_PROTOCOL_VERSION;
}

uint32_t passport_ble_bridge_revision(void)
{
    return s_summary_revision;
}

bool passport_ble_bridge_copy_summary(work_assistant_summary_t *summary)
{
    if (!summary || !s_summary_mutex) return false;
    if (xSemaphoreTake(s_summary_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool available = s_has_summary && s_summary;
    if (available) *summary = *s_summary;
    xSemaphoreGive(s_summary_mutex);
    return available;
}
