#include "radio/radio_cmd.h"
#include "protocol/packet_parser.h"
#include "radio/radio.h"
#include "uart_handler.h"
#include "protocol/payload_utils.h"
#include "config.h"

#include <zephyr/sys/reboot.h>
#include <zephyr/drivers/rtc.h>

void send_ack(uint16_t id) {
    send_packet(0xFF, NULL, 0, id);
}

void send_nack(uint16_t id) {
    send_packet(0xFE, NULL, 0, id);
}

void handle_rtc_set_time(const uint8_t *payload, uint8_t length, uint16_t id) {
    payload_cursor_t cursor;
    cursor_init(&cursor, payload, length);

    if (cursor.remaining < 7) {
        send_nack(id);
        return;
    }

    struct rtc_time tm;
    tm.tm_year = cursor_get_u16(&cursor) - 1900;
    tm.tm_mon = cursor_get_u8(&cursor) - 1;
    tm.tm_mday = cursor_get_u8(&cursor);
    tm.tm_hour = cursor_get_u8(&cursor);
    tm.tm_min = cursor_get_u8(&cursor);
    tm.tm_sec = cursor_get_u8(&cursor);

    if (rtc_set_time(rtc_dev, &tm) == 0) {
        send_ack(id);
    } else {
        send_nack(id);
    }
}

void handle_rtc_get_time(const uint8_t *payload, uint8_t length, uint16_t id) {
    struct rtc_time tm;

    if (rtc_get_time(rtc_dev, &tm) != 0) {
        send_nack(id);
        return;
    }

    uint8_t buffer[16]; 
    payload_writer_t writer;
    writer_init(&writer, buffer, sizeof(buffer));

    writer_put_u16(&writer, tm.tm_year + 1900);
    
    writer_put_u8(&writer, tm.tm_mon + 1);
    
    writer_put_u8(&writer, tm.tm_mday);
    writer_put_u8(&writer, tm.tm_hour);
    writer_put_u8(&writer, tm.tm_min);
    writer_put_u8(&writer, tm.tm_sec);

    if (writer.error) {
        send_nack(id);
    } else {
        size_t payload_len = writer.ptr - buffer;
        send_packet(0x02, buffer, payload_len, id);
    }
}

// base freq is 100 times the actual frequency to avoid floating point issues
void handle_set_base_freq(const uint8_t *payload, uint8_t length, uint16_t id) {
    payload_cursor_t cursor;
    cursor_init(&cursor, payload, length);

    if (cursor.remaining < 8) {
        send_nack(id);
        return;
    }

    uint64_t freq = cursor_get_u64(&cursor);
    base_frequency = freq;

    if (cursor.error) {
        send_nack(id);
    } else {
        send_ack(id);
    }
}

void handle_get_base_freq(const uint8_t *payload, uint8_t length, uint16_t id) {
    uint8_t buffer[8];
    payload_writer_t writer;
    writer_init(&writer, buffer, sizeof(buffer));

    writer_put_u64(&writer, base_frequency);

    if (writer.error) {
        send_nack(id);
    } else {
        size_t payload_len = writer.ptr - buffer;
        send_packet(0x04, buffer, payload_len, id);
    }
}

void handle_reset(const uint8_t *payload, uint8_t length, uint16_t id) {
    sys_reboot(SYS_REBOOT_COLD);
}
