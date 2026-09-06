#pragma once

#include "work_assistant_model.h"

#include <stdbool.h>

#define PASSPORT_BLE_DEVICE_NAME "FoloPassport"
#define PASSPORT_BLE_SERVICE_UUID "7d2ea28a-f7bd-485a-bd9d-92ad6ecfe93e"
#define PASSPORT_BLE_WRITE_UUID "7d2ea28b-f7bd-485a-bd9d-92ad6ecfe93e"
#define PASSPORT_BLE_STATUS_UUID "7d2ea28c-f7bd-485a-bd9d-92ad6ecfe93e"
#define PASSPORT_BLE_COMPLETIONS_UUID "7d2ea28d-f7bd-485a-bd9d-92ad6ecfe93e"
#define PASSPORT_BLE_COMPLETIONS_ACK_UUID "7d2ea28e-f7bd-485a-bd9d-92ad6ecfe93e"
#define PASSPORT_BLE_PROTOCOL_UUID "7d2ea28f-f7bd-485a-bd9d-92ad6ecfe93e"

typedef enum {
    PASSPORT_BLE_BRIDGE_IDLE = 0,
    PASSPORT_BLE_BRIDGE_STARTING,
    PASSPORT_BLE_BRIDGE_ADVERTISING,
    PASSPORT_BLE_BRIDGE_CONNECTED,
    PASSPORT_BLE_BRIDGE_PAIRING,
    PASSPORT_BLE_BRIDGE_RECEIVING,
    PASSPORT_BLE_BRIDGE_READY,
    PASSPORT_BLE_BRIDGE_ERROR,
} passport_ble_bridge_state_t;

bool passport_ble_bridge_start(void);
bool passport_ble_bridge_stop(void);
passport_ble_bridge_state_t passport_ble_bridge_state(void);
const char *passport_ble_bridge_status_message(void);
bool passport_ble_bridge_pairing_code(uint32_t *code);
uint32_t passport_ble_bridge_revision(void);
bool passport_ble_bridge_copy_summary(work_assistant_summary_t *summary);
bool passport_ble_bridge_serial_number(char out[13]);
bool passport_ble_bridge_enqueue_completion(const char *guid);
uint8_t passport_ble_bridge_protocol_version(void);
