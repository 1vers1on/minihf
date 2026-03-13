#ifndef RADIO_RECEIVER_H
#define RADIO_RECEIVER_H
#include <stdint.h>

void receiver_init(void);
void receiver_start(void);
void receiver_stop(void);

extern uint8_t fft_buffer[128];

#endif // RADIO_RECEIVER_H
