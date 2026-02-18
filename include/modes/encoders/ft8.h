#ifndef MODES_ENCODERS_FT8_H
#define MODES_ENCODERS_FT8_H

#include <stdint.h>
#include "radio_core.h"
#include "modes/ftx.h"

void generate_ft8_sequence(const ftx_payload_t* payload, const tx_sequence_t* tx_sequence);

#endif // MODES_ENCODERS_FT8_H