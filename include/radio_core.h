#ifndef RADIO_MODE_H
#define RADIO_MODE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    uint16_t freq_offset_hz;
    uint32_t duration_us;
    bool     tx_on;
} tx_symbol_t;

typedef struct {
    const char* mode_name;
    uint32_t base_freq_hz;
    
    const tx_symbol_t* symbols; 
    size_t total_symbols;
    
    // Runtime state
    size_t current_index;
    bool repeat;
} tx_sequence_t;

#endif // RADIO_MODE_H
