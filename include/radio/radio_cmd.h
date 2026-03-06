#ifndef RADIO_CMD_H
#define RADIO_CMD_H

#include <stdint.h>
#include <stddef.h>

void handle_rtc_set_time(const uint8_t *payload, uint8_t length, uint16_t id);
void handle_rtc_get_time(const uint8_t *payload, uint8_t length, uint16_t id);
void handle_set_base_freq(const uint8_t *payload, uint8_t length, uint16_t id);
void handle_get_base_freq(const uint8_t *payload, uint8_t length, uint16_t id);
void handle_set_buck_boost_regulator(const uint8_t *payload, uint8_t length, uint16_t id);
void handle_get_buck_boost_regulator(const uint8_t *payload, uint8_t length, uint16_t id);
void handle_reset(const uint8_t *payload, uint8_t length, uint16_t id);
void handle_tx_test_signal(const uint8_t *payload, uint8_t length, uint16_t id);

void send_debug_message(const char *message);

#endif // RADIO_CMD_H