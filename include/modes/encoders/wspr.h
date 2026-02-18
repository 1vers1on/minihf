#ifndef MODES_ENCODERS_WSPR_H
#define MODES_ENCODERS_WSPR_H

#include <stdint.h>
#include "radio_core.h"

typedef struct {
    char callsign[7];
    char grid[5];
    int power_dbm;
} wspr_payload_t;

int generate_wspr_sequence(const wspr_payload_t* payload, tx_sequence_t* tx_sequence);

#endif // MODES_ENCODERS_WSPR_H
