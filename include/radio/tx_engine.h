#ifndef RADIO_TX_ENGINE_H
#define RADIO_TX_ENGINE_H

#include "radio_core.h"

void tx_engine_init();

void tx_engine_start(tx_sequence_t *seq);

void tx_engine_stop();

bool tx_engine_is_active();

#endif /* RADIO_TX_ENGINE_H */
