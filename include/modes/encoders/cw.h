#ifndef MODES_ENCODERS_CW_H
#define MODES_ENCODERS_CW_H

#include <stdint.h>
#include "radio_core.h"

void generate_cw_sequence(const char* text, uint32_t wpm, tx_sequence_t* tx_sequence);

#endif // MODES_ENCODERS_CW_H