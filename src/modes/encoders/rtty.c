#include "modes/encoders/rtty.h"
#include "radio_core.h"

#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <zephyr/kernel.h>

#define BAUDOT_LTRS_SHIFT 0x1F
#define BAUDOT_FIGS_SHIFT 0x1B
#define BAUDOT_SPACE      0x04
#define BAUDOT_CR         0x08
#define BAUDOT_LF         0x02

const char ITA2_LTRS[32] = {
    '\0', 'E', '\n', 'A', ' ', 'S', 'I', 'U',
    '\r', 'D', 'R', 'J', 'N', 'F', 'C', 'K',
    'T', 'Z', 'L', 'W', 'H', 'Y', 'P', 'Q', 'O',
    'B', 'G', '\0', 'M', 'X', 'V', '\0'
};

const char ITA2_FIGS[32] = {
    '\0', '3', '\n', '-', ' ', '\'', '8', '7',
    '\r', '\x05', '4', '\a', ',', '!', ':', '(',
    '5', '+', ')', '2', '#' , '6', '0', '1', '9',
    '?', '&', '\0', '.', '/', '=', '\0'
};

typedef enum { SHIFT_ANY, SHIFT_LTRS, SHIFT_FIGS } shift_state_t;

static bool ascii_to_ita2(char c, uint8_t* code, shift_state_t* shift_state) {
    c = toupper((unsigned char)c);

    if (c == ' ') {
        *code = BAUDOT_SPACE;
        *shift_state = SHIFT_ANY;
        return true;
    }

    if (c == '\r') {
        *code = BAUDOT_CR;
        *shift_state = SHIFT_ANY;
        return true;
    }

    if (c == '\n') {
        *code = BAUDOT_LF;
        *shift_state = SHIFT_ANY;
        return true;
    }

    for (int i = 0; i < 32; i++) {
        if (ITA2_LTRS[i] == c) {
            *code = (uint8_t)i;
            *shift_state = SHIFT_LTRS;
            return true;
        }
    }

    for (int i = 0; i < 32; i++) {
        if (ITA2_FIGS[i] == c) {
            *code = (uint8_t)i;
            *shift_state = SHIFT_FIGS;
            return true;
        }
    }

    return false;
}

static void push_baudot(uint8_t baudot_code, tx_symbol_t* symbols, size_t* index,
                        uint32_t bit_usec, uint32_t stop_usec, float mark_offset, 
                        float space_offset) {
    symbols[*index] = (tx_symbol_t){space_offset, bit_usec, true};
    (*index)++;

    for (int b = 0; b < 5; b++) {
        bool is_mark = (baudot_code >> b) & 0x01;
        symbols[*index] = (tx_symbol_t){is_mark ? mark_offset : space_offset, bit_usec, true};
        (*index)++;
    }

    symbols[*index] = (tx_symbol_t){mark_offset, stop_usec, true};
    (*index)++;
}

int generate_rtty_sequence(const char* text, const rtty_config_t* config, 
                            tx_sequence_t* tx_sequence) {
    if (!text || !config || !tx_sequence) {
        return -1;
    }

    uint32_t bit_usec = (uint32_t)(1000000.0f / config->baud_rate);
    uint32_t stop_us = (uint32_t)(bit_usec * config->stop_bits);

    float mark_offset, space_offset;
    
    if (config->use_center_freq) {
        float half_shift = config->shift_hz / 2.0f;
        mark_offset  = config->reverse_shift ? -half_shift : half_shift;
        space_offset = config->reverse_shift ? half_shift : -half_shift;
    } else {
        mark_offset  = config->reverse_shift ? config->shift_hz : 0;
        space_offset = config->reverse_shift ? 0 : config->shift_hz;
    }

    size_t baudot_char_count = 0;
    shift_state_t current_state = SHIFT_LTRS;
    size_t text_len = strlen(text);

    for (size_t i = 0; i < text_len; i++) {
        uint8_t code;
        shift_state_t req_shift;
        
        if (ascii_to_ita2(text[i], &code, &req_shift)) {
            if (req_shift != SHIFT_ANY && req_shift != current_state) {
                baudot_char_count++; 
                current_state = req_shift;
            }
            baudot_char_count++; 
        }
    }

    size_t required_symbols = baudot_char_count * 7;

    tx_sequence->symbols = (tx_symbol_t*)k_malloc(required_symbols * sizeof(tx_symbol_t));
    if (!tx_sequence->symbols) return -2; 

    tx_sequence->total_symbols = required_symbols;
    tx_sequence->current_index = 0;

    size_t sym_idx = 0;
    current_state = SHIFT_LTRS;

    for (size_t i = 0; i < text_len; i++) {
        uint8_t code;
        shift_state_t req_shift;
        
        if (ascii_to_ita2(text[i], &code, &req_shift)) {
            
            if (req_shift != SHIFT_ANY && req_shift != current_state) {
                uint8_t shift_code = (req_shift == SHIFT_LTRS) ? BAUDOT_LTRS_SHIFT : BAUDOT_FIGS_SHIFT;
                push_baudot(shift_code, tx_sequence->symbols, &sym_idx, bit_usec, stop_us, mark_offset, space_offset);
                current_state = req_shift;
            }
            
            push_baudot(code, tx_sequence->symbols, &sym_idx, bit_usec, stop_us, mark_offset, space_offset);
        }
    }

    return 0;
}
