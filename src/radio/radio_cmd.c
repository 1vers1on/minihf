#include "radio/radio_cmd.h"
#include "protocol/packet_parser.h"
#include "radio/radio.h"
#include "radio/tx_engine.h"
#include "uart_handler.h"
#include "protocol/payload_utils.h"
#include "config.h"
#include "zephyr/drivers/regulator.h"

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

void handle_set_buck_regulator(const uint8_t *payload, uint8_t length, uint16_t id) {
    payload_cursor_t cursor;
    cursor_init(&cursor, payload, length);

    if (cursor.remaining < 1) {
        send_nack(id);
        return;
    }

    uint8_t state = cursor_get_u8(&cursor);
    bool buck_regulator_enabled = (state & 0x80) != 0;
    uint8_t voltage_level = state & 0x1F;

    regulator_set_voltage(regulator, voltage_level * 1000000, voltage_level * 1000000);

    if (buck_regulator_enabled) {
        regulator_enable(regulator);
    } else {
        regulator_disable(regulator);
    }
}

void handle_get_buck_regulator(const uint8_t *payload, uint8_t length, uint16_t id) {
    uint8_t buffer[1];
    payload_writer_t writer;
    writer_init(&writer, buffer, sizeof(buffer));

    int32_t volt_uv;
    int ret = regulator_get_voltage(regulator, &volt_uv);
    if (ret < 0) {
        send_nack(id);
        return;
    }

    uint8_t voltage_level = volt_uv / 1000000;
    bool buck_regulator_enabled = regulator_is_enabled(regulator);

    uint8_t state = (buck_regulator_enabled ? 0x80 : 0x00) | (voltage_level & 0x1F);
    writer_put_u8(&writer, state);

    if (writer.error) {
        send_nack(id);
    } else {
        size_t payload_len = writer.ptr - buffer;
        send_packet(0x06, buffer, payload_len, id);
    }

}

void handle_reset(const uint8_t *payload, uint8_t length, uint16_t id) {
    sys_reboot(SYS_REBOOT_COLD);
}

static tx_symbol_t test_signal_symbol;
static tx_sequence_t test_signal_seq;

void handle_tx_test_signal(const uint8_t *payload, uint8_t length, uint16_t id) {
    payload_cursor_t cursor;
    cursor_init(&cursor, payload, length);

    if (cursor.remaining < 4) {
        send_nack(id);
        return;
    }

    uint32_t duration_ms = cursor_get_u32(&cursor);

    if (cursor.error || duration_ms == 0) {
        send_nack(id);
        return;
    }

    test_signal_symbol.freq_offset_hz = 0.0f;
    test_signal_symbol.duration_us = duration_ms * 1000U;
    test_signal_symbol.tx_on = true;

    test_signal_seq.mode_name = "test";
    test_signal_seq.base_freq_hz = (uint32_t)(base_frequency / 100U);
    test_signal_seq.symbols = &test_signal_symbol;
    test_signal_seq.total_symbols = 1;
    test_signal_seq.current_index = 0;
    test_signal_seq.repeat = false;

    tx_engine_start(&test_signal_seq);
    send_ack(id);
}
