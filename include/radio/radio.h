#ifndef RADIO_RADIO_H
#define RADIO_RADIO_H

#include <stdint.h>

extern uint64_t base_frequency;

#define BAND_30M_MIN_FREQ  (10100000ULL * 100)
#define BAND_30M_MAX_FREQ  (10150000ULL * 100)

static inline uint64_t clamp_frequency(uint64_t freq) {
    if (freq < BAND_30M_MIN_FREQ) return BAND_30M_MIN_FREQ;
    if (freq > BAND_30M_MAX_FREQ) return BAND_30M_MAX_FREQ;
    return freq;
}

#endif // RADIO_RADIO_H