#include "passport_ble_protocol.h"

#include <assert.h>
#include <string.h>

static void write_u32_le(uint8_t *target, uint32_t value)
{
    target[0] = value & 0xFF;
    target[1] = (value >> 8) & 0xFF;
    target[2] = (value >> 16) & 0xFF;
    target[3] = (value >> 24) & 0xFF;
}

static void begin_transfer(passport_ble_receiver_t *receiver,
                           const uint8_t *payload, size_t length)
{
    uint8_t begin[9] = { PASSPORT_BLE_OP_BEGIN };
    write_u32_le(begin + 1, (uint32_t)length);
    write_u32_le(begin + 5, passport_ble_crc32(payload, length));
    assert(passport_ble_receiver_write(receiver, begin, sizeof(begin)) ==
           PASSPORT_BLE_FRAME_ACCEPTED);
}

static void test_complete_transfer(void)
{
    const uint8_t payload[] = "{\"version\":1}";
    passport_ble_receiver_t receiver;
    passport_ble_receiver_init(&receiver);
    begin_transfer(&receiver, payload, sizeof(payload) - 1);

    uint8_t first[] = { PASSPORT_BLE_OP_DATA, 0, 0, '{', '"' };
    assert(passport_ble_receiver_write(&receiver, first, sizeof(first)) ==
           PASSPORT_BLE_FRAME_ACCEPTED);

    uint8_t second[3 + sizeof(payload) - 3] = {
        PASSPORT_BLE_OP_DATA, 1, 0,
    };
    memcpy(second + 3, payload + 2, sizeof(payload) - 3);
    assert(passport_ble_receiver_write(&receiver, second, sizeof(second)) ==
           PASSPORT_BLE_FRAME_ACCEPTED);

    uint8_t commit[] = { PASSPORT_BLE_OP_COMMIT };
    assert(passport_ble_receiver_write(&receiver, commit, sizeof(commit)) ==
           PASSPORT_BLE_FRAME_COMPLETE);
    assert(strcmp((char *)receiver.payload, (char *)payload) == 0);
}

static void test_rejects_bad_sequence_and_crc(void)
{
    const uint8_t payload[] = "{}";
    passport_ble_receiver_t receiver;
    passport_ble_receiver_init(&receiver);
    begin_transfer(&receiver, payload, sizeof(payload) - 1);
    uint8_t out_of_order[] = { PASSPORT_BLE_OP_DATA, 1, 0, '{', '}' };
    assert(passport_ble_receiver_write(&receiver, out_of_order,
                                       sizeof(out_of_order)) ==
           PASSPORT_BLE_FRAME_BAD_SEQUENCE);

    uint8_t begin[9] = { PASSPORT_BLE_OP_BEGIN };
    write_u32_le(begin + 1, 2);
    write_u32_le(begin + 5, 0);
    assert(passport_ble_receiver_write(&receiver, begin, sizeof(begin)) ==
           PASSPORT_BLE_FRAME_ACCEPTED);
    uint8_t data[] = { PASSPORT_BLE_OP_DATA, 0, 0, '{', '}' };
    assert(passport_ble_receiver_write(&receiver, data, sizeof(data)) ==
           PASSPORT_BLE_FRAME_ACCEPTED);
    uint8_t commit[] = { PASSPORT_BLE_OP_COMMIT };
    assert(passport_ble_receiver_write(&receiver, commit, sizeof(commit)) ==
           PASSPORT_BLE_FRAME_BAD_CRC);
}

static void test_rejects_oversize_and_truncated(void)
{
    passport_ble_receiver_t receiver;
    passport_ble_receiver_init(&receiver);
    uint8_t begin[9] = { PASSPORT_BLE_OP_BEGIN };
    write_u32_le(begin + 1, PASSPORT_BLE_PAYLOAD_MAX + 1);
    assert(passport_ble_receiver_write(&receiver, begin, sizeof(begin)) ==
           PASSPORT_BLE_FRAME_TOO_LARGE);

    const uint8_t payload[] = "{}";
    begin_transfer(&receiver, payload, sizeof(payload) - 1);
    uint8_t commit[] = { PASSPORT_BLE_OP_COMMIT };
    assert(passport_ble_receiver_write(&receiver, commit, sizeof(commit)) ==
           PASSPORT_BLE_FRAME_BAD_FORMAT);
}

int main(void)
{
    test_complete_transfer();
    test_rejects_bad_sequence_and_crc();
    test_rejects_oversize_and_truncated();
    return 0;
}
