#ifndef PROTOCOL_PACKET_PARSER_H
#define PROTOCOL_PACKET_PARSER_H

#include <stdint.h>
#include <stddef.h>

typedef struct __attribute__((packed)) {
    uint8_t header;
    uint8_t type;
    uint16_t id;
    uint8_t length;
    uint8_t  payload_and_crc[];
} packet_t;

typedef void (*packet_handler_t)(const uint8_t *payload, uint8_t length, uint16_t id);

typedef struct {
    uint8_t cmd_id;
    packet_handler_t handler;
} cmd_entry_t;

void parse_dispatch_packet(const uint8_t *data, size_t length);
void send_packet(uint8_t cmd_id, const uint8_t *payload, size_t payload_len, uint16_t id);

#endif // PROTOCOL_PACKET_PARSER_H