#include "protocol/packet_parser.h"
#include <zephyr/sys/crc.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include "uart_handler.h"
#include "radio/radio_cmd.h"

LOG_MODULE_REGISTER(packet_parser);

static const cmd_entry_t cmd_table[] = {
    {0x01, handle_rtc_set_time},
    {0x02, handle_rtc_get_time},
    {0x03, handle_set_base_freq},
    {0x04, handle_get_base_freq},
    {0xFF, handle_reset},
};

void parse_dispatch_packet(const uint8_t *data, size_t length) {
    if (length < sizeof(packet_t) + 2) {
        return;
    }

    const packet_t *pkt = (const packet_t *)data;

    if (pkt->header != 0xAA) {
        return;
    }

    size_t expected_len = sizeof(packet_t) + pkt->length + 2;
    if (length < expected_len) {
        LOG_ERR("Packet too short: expected %zu, got %zu", expected_len, length);
        return;
    }

    uint16_t crc_calculated = crc16_ccitt(0x0000, data, sizeof(packet_t) + pkt->length);
    uint16_t crc_received = sys_get_le16(&pkt->payload_and_crc[pkt->length]);
    if (crc_calculated != crc_received) {
        LOG_ERR("CRC mismatch: calculated 0x%04X, received 0x%04X", crc_calculated, crc_received);
        return;
    }

    for (size_t i = 0; i < sizeof(cmd_table) / sizeof(cmd_entry_t); i++) {
        if (cmd_table[i].cmd_id == pkt->type) {
            cmd_table[i].handler(pkt->payload_and_crc, pkt->length, pkt->id);
            return;
        }
    }

    LOG_WRN("No handler found for packet type 0x%02X", pkt->type);
}

void send_packet(uint8_t cmd_id, const uint8_t *payload, size_t payload_len, uint16_t id) {
    if (payload_len > 255) {
        return;
    }

    uint8_t buf[sizeof(packet_t) + 255 + 2];
    packet_t *pkt = (packet_t *)buf;

    pkt->header = 0xAA;
    pkt->type = cmd_id;
    pkt->id = id;
    pkt->length = payload_len;
    memcpy(pkt->payload_and_crc, payload, payload_len);

    size_t pkt_size = sizeof(packet_t) + payload_len;
    uint16_t crc = crc16_ccitt(0x0000, buf, pkt_size);
    sys_put_le16(crc, &pkt->payload_and_crc[payload_len]);

    send_uart_data(buf, pkt_size + 2);
}
