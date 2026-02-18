#include "modes/encoders/wspr.h"
#include "radio_core.h"

#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <zephyr/kernel.h>

#define WSPR_SYMBOL_COUNT   162
#define WSPR_MSG_BYTES      11

static const uint8_t sync_vector[WSPR_SYMBOL_COUNT] = {
    1,1,0,0,0,0,0,0,1,0,0,0,1,1,1,0,0,0,1,0,
    0,1,0,1,1,1,1,0,0,0,0,0,0,0,1,0,0,1,0,1,
    0,0,0,0,0,0,1,0,1,1,0,0,1,1,0,1,0,0,0,1,
    1,0,1,0,0,0,0,1,1,0,1,0,1,0,1,0,1,0,0,1,
    0,0,1,0,1,1,0,0,0,1,1,0,1,0,1,0,0,0,1,0,
    0,0,0,0,1,0,0,1,0,0,1,1,1,0,1,1,0,0,1,1,
    0,1,0,0,0,1,1,1,0,0,0,0,0,1,0,1,0,0,1,1,
    0,0,0,0,0,0,0,1,1,0,1,0,1,1,0,0,0,1,1,0,
    0,0
};

#define WSPR_SYMBOL_US      682667U
#define WSPR_TONE_SPACING   1.4648f

static const int valid_powers[] = {
    0, 3, 7, 10, 13, 17, 20, 23, 27, 30, 33, 37, 40, 43, 47, 50, 53, 57, 60
};
#define NUM_VALID_POWERS (sizeof(valid_powers) / sizeof(valid_powers[0]))

static bool is_valid_power(int power) {
    for (size_t i = 0; i < NUM_VALID_POWERS; i++) {
        if (valid_powers[i] == power) {
            return true;
        }
    }
    return false;
}

static int grid_char_to_index(char c) {
    c = toupper((unsigned char)c);
    if (c >= 'A' && c <= 'R') {
        return c - 'A';
    }
    return -1;
}

static int callsign_char_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 10;
    }
    return 36;
}

static uint8_t parity(uint32_t v) {
    v ^= v >> 16;
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (uint8_t)(v & 1);
}

static uint8_t bit_reverse8(uint8_t v) {
    v = ((v & 0xF0) >> 4) | ((v & 0x0F) << 4);
    v = ((v & 0xCC) >> 2) | ((v & 0x33) << 2);
    v = ((v & 0xAA) >> 1) | ((v & 0x55) << 1);
    return v;
}

static bool validate_callsign(const char* callsign) {
    size_t len = strlen(callsign);
    if (len < 2 || len > 6) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = callsign[i];
        if (!isalnum((unsigned char)c) && c != '/') {
            return false;
        }
    }
    return true;
}

static bool validate_grid(const char* grid) {
    if (strlen(grid) != 4) {
        return false;
    }
    char c0 = toupper((unsigned char)grid[0]);
    char c1 = toupper((unsigned char)grid[1]);
    if (c0 < 'A' || c0 > 'R' || c1 < 'A' || c1 > 'R') {
        return false;
    }
    if (!isdigit((unsigned char)grid[2]) || !isdigit((unsigned char)grid[3])) {
        return false;
    }
    return true;
}

static void pad_callsign(const char* callsign, char padded[7]) {
    size_t len = strlen(callsign);
    int dst = 0;

    if (len >= 2 && isalpha((unsigned char)callsign[0]) &&
        isdigit((unsigned char)callsign[1])) {
        padded[dst++] = ' ';
    }

    for (size_t i = 0; i < len && dst < 6; i++) {
        padded[dst++] = toupper((unsigned char)callsign[i]);
    }

    while (dst < 6) {
        padded[dst++] = ' ';
    }
    padded[6] = '\0';
}

static uint32_t encode_callsign(const char* callsign) {
    char padded[7];
    pad_callsign(callsign, padded);

    uint32_t N = callsign_char_value(padded[0]);
    N = N * 36 + callsign_char_value(padded[1]);
    N = N * 10 + callsign_char_value(padded[2]);
    N = N * 27 + (callsign_char_value(padded[3]) - 10);
    N = N * 27 + (callsign_char_value(padded[4]) - 10);
    N = N * 27 + (callsign_char_value(padded[5]) - 10);

    return N;
}

static int32_t encode_grid_power(const char* grid, int power) {
    int loc1 = grid_char_to_index(grid[0]);
    int loc2 = grid_char_to_index(grid[1]);
    int loc3 = grid[2] - '0';
    int loc4 = grid[3] - '0';

    if (loc1 < 0 || loc2 < 0) {
        return -1;
    }

    int32_t M = (179 - 10 * loc1 - loc3) * 180 + 10 * loc2 + loc4;
    M = M * 128 + power + 64;

    return M;
}

static void pack_message(uint32_t N, int32_t M, uint8_t packed[WSPR_MSG_BYTES]) {
    memset(packed, 0, WSPR_MSG_BYTES);

    packed[0] = (N >> 20) & 0xFF;
    packed[1] = (N >> 12) & 0xFF;
    packed[2] = (N >>  4) & 0xFF;
    packed[3] = ((N & 0x0F) << 4) | ((M >> 18) & 0x0F);
    packed[4] = (M >> 10) & 0xFF;
    packed[5] = (M >>  2) & 0xFF;
    packed[6] = (M & 0x03) << 6;
}

static void convolve(const uint8_t packed[WSPR_MSG_BYTES],
                     uint8_t convolved[WSPR_SYMBOL_COUNT]) {
    uint32_t shift_reg = 0;
    int out_idx = 0;

    for (int i = 0; i < WSPR_MSG_BYTES && out_idx < WSPR_SYMBOL_COUNT; i++) {
        for (int j = 7; j >= 0 && out_idx < WSPR_SYMBOL_COUNT; j--) {
            uint8_t input_bit = (packed[i] >> j) & 1;
            shift_reg = (shift_reg << 1) | input_bit;

            convolved[out_idx++] = parity(shift_reg & 0xF2D05351);
            convolved[out_idx++] = parity(shift_reg & 0xE4613C47);
        }
    }
}

static void interleave(const uint8_t convolved[WSPR_SYMBOL_COUNT],
                       uint8_t interleaved[WSPR_SYMBOL_COUNT]) {
    int src = 0;

    for (int j = 0; j < 256 && src < WSPR_SYMBOL_COUNT; j++) {
        uint8_t rev = bit_reverse8((uint8_t)j);
        if (rev < WSPR_SYMBOL_COUNT) {
            interleaved[rev] = convolved[src++];
        }
    }
}

static void merge_sync(const uint8_t interleaved[WSPR_SYMBOL_COUNT],
                       uint8_t symbols[WSPR_SYMBOL_COUNT]) {
    for (int i = 0; i < WSPR_SYMBOL_COUNT; i++) {
        symbols[i] = interleaved[i] * 2 + sync_vector[i];
    }
}

static int wspr_encode(const char* callsign, const char* grid, int power,
                       uint8_t symbols[WSPR_SYMBOL_COUNT]) {
    uint32_t N = encode_callsign(callsign);
    int32_t  M = encode_grid_power(grid, power);

    if (M < 0) {
        return -1;
    }

    uint8_t packed[WSPR_MSG_BYTES];
    pack_message(N, M, packed);

    uint8_t convolved[WSPR_SYMBOL_COUNT];
    convolve(packed, convolved);

    uint8_t interleaved[WSPR_SYMBOL_COUNT];
    interleave(convolved, interleaved);

    merge_sync(interleaved, symbols);

    return 0;
}

int generate_wspr_sequence(const wspr_payload_t* payload, tx_sequence_t* tx_sequence) {
    tx_sequence->mode_name = "WSPR";

    if (!payload || !tx_sequence) {
        return -1;
    }

    if (!validate_callsign(payload->callsign)) {
        return -1;
    }

    if (!validate_grid(payload->grid)) {
        return -1;
    }

    if (!is_valid_power(payload->power_dbm)) {
        return -1;
    }

    uint8_t channel_symbols[WSPR_SYMBOL_COUNT];
    if (wspr_encode(payload->callsign, payload->grid,
                    payload->power_dbm, channel_symbols) != 0) {
        return -1;
    }

    tx_sequence->symbols = (tx_symbol_t *)k_malloc(
        WSPR_SYMBOL_COUNT * sizeof(tx_symbol_t));
    if (!tx_sequence->symbols) {
        return -2;
    }

    for (int i = 0; i < WSPR_SYMBOL_COUNT; i++) {
        tx_sequence->symbols[i] = (tx_symbol_t){
            .freq_offset_hz = channel_symbols[i] * WSPR_TONE_SPACING,
            .duration_us    = WSPR_SYMBOL_US,
            .tx_on          = true,
        };
    }

    tx_sequence->total_symbols = WSPR_SYMBOL_COUNT;
    tx_sequence->current_index = 0;

    return 0;
}
