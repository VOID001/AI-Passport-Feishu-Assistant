#include "passport_ble_protocol.h"

#include <string.h>

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

uint32_t passport_ble_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

void passport_ble_receiver_init(passport_ble_receiver_t *receiver)
{
    if (!receiver) return;
    memset(receiver, 0, sizeof(*receiver));
}

passport_ble_frame_result_t passport_ble_receiver_write(
    passport_ble_receiver_t *receiver, const uint8_t *frame, size_t frame_length)
{
    if (!receiver || !frame || frame_length == 0) {
        return PASSPORT_BLE_FRAME_BAD_FORMAT;
    }

    switch (frame[0]) {
    case PASSPORT_BLE_OP_BEGIN:
        if (frame_length != 9) return PASSPORT_BLE_FRAME_BAD_FORMAT;
        receiver->expected_length = read_u32_le(frame + 1);
        receiver->expected_crc = read_u32_le(frame + 5);
        receiver->received_length = 0;
        receiver->next_sequence = 0;
        receiver->active = receiver->expected_length > 0 &&
                           receiver->expected_length <= PASSPORT_BLE_PAYLOAD_MAX;
        return receiver->active ? PASSPORT_BLE_FRAME_ACCEPTED
                                : PASSPORT_BLE_FRAME_TOO_LARGE;

    case PASSPORT_BLE_OP_DATA: {
        if (!receiver->active) return PASSPORT_BLE_FRAME_BAD_STATE;
        if (frame_length <= 3) return PASSPORT_BLE_FRAME_BAD_FORMAT;
        uint16_t sequence = read_u16_le(frame + 1);
        if (sequence != receiver->next_sequence) {
            receiver->active = false;
            return PASSPORT_BLE_FRAME_BAD_SEQUENCE;
        }
        size_t chunk_length = frame_length - 3;
        if (receiver->received_length + chunk_length > receiver->expected_length) {
            receiver->active = false;
            return PASSPORT_BLE_FRAME_TOO_LARGE;
        }
        memcpy(receiver->payload + receiver->received_length, frame + 3, chunk_length);
        receiver->received_length += chunk_length;
        receiver->next_sequence++;
        return PASSPORT_BLE_FRAME_ACCEPTED;
    }

    case PASSPORT_BLE_OP_COMMIT:
        if (frame_length != 1 || !receiver->active) {
            return PASSPORT_BLE_FRAME_BAD_STATE;
        }
        receiver->active = false;
        if (receiver->received_length != receiver->expected_length) {
            return PASSPORT_BLE_FRAME_BAD_FORMAT;
        }
        if (passport_ble_crc32(receiver->payload, receiver->received_length) !=
            receiver->expected_crc) {
            return PASSPORT_BLE_FRAME_BAD_CRC;
        }
        receiver->payload[receiver->received_length] = '\0';
        return PASSPORT_BLE_FRAME_COMPLETE;

    case PASSPORT_BLE_OP_CANCEL:
        if (frame_length != 1) return PASSPORT_BLE_FRAME_BAD_FORMAT;
        passport_ble_receiver_init(receiver);
        return PASSPORT_BLE_FRAME_ACCEPTED;

    default:
        return PASSPORT_BLE_FRAME_BAD_FORMAT;
    }
}
