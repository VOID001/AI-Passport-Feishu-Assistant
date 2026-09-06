#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PASSPORT_BLE_PROTOCOL_VERSION 4
#define PASSPORT_BLE_PAYLOAD_MAX 12288

#define PASSPORT_BLE_OP_BEGIN 0x01
#define PASSPORT_BLE_OP_DATA 0x02
#define PASSPORT_BLE_OP_COMMIT 0x03
#define PASSPORT_BLE_OP_CANCEL 0x04

typedef enum {
    PASSPORT_BLE_FRAME_ACCEPTED = 0,
    PASSPORT_BLE_FRAME_COMPLETE,
    PASSPORT_BLE_FRAME_BAD_FORMAT,
    PASSPORT_BLE_FRAME_BAD_STATE,
    PASSPORT_BLE_FRAME_BAD_SEQUENCE,
    PASSPORT_BLE_FRAME_TOO_LARGE,
    PASSPORT_BLE_FRAME_BAD_CRC,
} passport_ble_frame_result_t;

typedef struct {
    uint8_t payload[PASSPORT_BLE_PAYLOAD_MAX + 1];
    uint32_t expected_length;
    uint32_t expected_crc;
    uint32_t received_length;
    uint16_t next_sequence;
    bool active;
} passport_ble_receiver_t;

void passport_ble_receiver_init(passport_ble_receiver_t *receiver);
passport_ble_frame_result_t passport_ble_receiver_write(
    passport_ble_receiver_t *receiver, const uint8_t *frame, size_t frame_length);
uint32_t passport_ble_crc32(const uint8_t *data, size_t length);
