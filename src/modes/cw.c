#include "modes/cw.h"
#include "radio_core.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>


static const char* MORSE_TABLE[128] = {
    ['A'] = ".-",
    ['B'] = "-...",
    ['C'] = "-.-.",
    ['D'] = "-..",
    ['E'] = ".",
    ['F'] = "..-.",
    ['G'] = "--.",
    ['H'] = "....",
    ['I'] = "..",
    ['J'] = ".---",
    ['K'] = "-.-",
    ['L'] = ".-..",
    ['M'] = "--",
    ['N'] = "-.",
    ['O'] = "---",
    ['P'] = ".--.",
    ['Q'] = "--.-",
    ['R'] = ".-.",
    ['S'] = "...",
    ['T'] = "-",
    ['U'] = "..-",
    ['V'] = "...-",
    ['W'] = ".--",
    ['X'] = "-..-",
    ['Y'] = "-.--",
    ['Z'] = "--..",

    ['0'] = "-----",
    ['1'] = ".----",
    ['2'] = "..---",
    ['3'] = "...--",
    ['4'] = "....-",
    ['5'] = ".....",
    ['6'] = "-....",
    ['7'] = "--...",
    ['8'] = "---..",
    ['9'] = "----.",
};

static uint32_t calculate_dot_duration_us(uint32_t wpm) {
    return 1200000 / wpm;
}

void generate_cw_sequence(const char* text, uint32_t wpm, tx_sequence_t* tx_sequence) {
    tx_sequence->mode_name = "CW";

    uint32_t dot_us = calculate_dot_duration_us(wpm);
    uint32_t dash_us = 3 * dot_us;

    size_t estimated_capacity = 0;
    size_t len = strlen(text);

    for (size_t i = 0; i < len; i++) {
        char c = toupper((unsigned char)text[i]);
        if (c == ' ') {
            continue;
        } else if (c < 128 && MORSE_TABLE[(int)c]) {
            estimated_capacity += strlen(MORSE_TABLE[(int)c]) * 2;
        }
    }

    tx_symbol_t* sym_array = (tx_symbol_t*)k_malloc(estimated_capacity * sizeof(tx_symbol_t));
    if (!sym_array) {
        tx_sequence->symbols = NULL;
        tx_sequence->total_symbols = 0;
        return;
    }

    size_t sym_idx = 0;

    for (size_t i = 0; i < len; i++) {
        char c = toupper((unsigned char)text[i]);

        if (c == ' ') {
            if (sym_idx > 0 && !sym_array[sym_idx - 1].tx_on) {

                sym_array[sym_idx - 1].duration_us += (6 * dot_us);
            }
            continue;
        }

        const char* code = (c < 128) ? MORSE_TABLE[c] : NULL;

        if (code) {
            if (sym_idx > 0 && !sym_array[sym_idx - 1].tx_on) {
                sym_array[sym_idx - 1].duration_us += (2 * dot_us);
            }

            while (*code) {
                sym_array[sym_idx].tx_on = true;
                sym_array[sym_idx].freq_offset_hz = 0;
                
                if (*code == '.') {
                    sym_array[sym_idx].duration_us = dot_us;
                } else {
                    sym_array[sym_idx].duration_us = dash_us;
                }
                sym_idx++;

                sym_array[sym_idx].tx_on = false;
                sym_array[sym_idx].freq_offset_hz = 0;
                sym_array[sym_idx].duration_us = dot_us;
                sym_idx++;

                code++;
            }
        }
    }

    tx_sequence->symbols = sym_array;
    tx_sequence->total_symbols = sym_idx;
}
