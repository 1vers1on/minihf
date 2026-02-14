#ifndef MODES_RTTY_H
#define MODES_RTTY_H

#include <stdint.h>
#include "radio_core.h"

typedef struct {
    float    baud_rate;
    uint16_t shift_hz;
    float    stop_bits;
    bool     reverse_shift;
    bool     use_center_freq;
} rtty_config_t;

int generate_rtty_sequence(const char* text, const rtty_config_t* config, tx_sequence_t* tx_sequence);
#endif // MODES_RTTY_H