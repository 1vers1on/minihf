#include "modes/ftx.h"

#include <stdint.h>

#define C28_OFFSET_CQ_DIGITS 3
#define C28_OFFSET_CQ_CHAR  1004
#define MAXGRID4 32400

static void pack_bits(uint8_t *buf, int bit_pos, uint64_t value, int nbits) {
    for (int i = nbits - 1; i >= 0; i--) {
        int pos = bit_pos + (nbits - 1 - i);
        int byte_idx = pos / 8;
        int bit_idx = 7 - (pos % 8);
        if (value & (1ULL << i)) {
            buf[byte_idx] |= (1 << bit_idx);
        } else {
            buf[byte_idx] &= ~(1 << bit_idx);
        }
    }
}

static uint64_t unpack_bits(const uint8_t *buf, int bit_pos, int nbits) {
    uint64_t value = 0;
    for (int i = 0; i < nbits; i++) {
        int pos = bit_pos + i;
        int byte_idx = pos / 8;
        int bit_idx = 7 - (pos % 8);
        if (buf[byte_idx] & (1 << bit_idx)) {
            value |= (1ULL << (nbits - 1 - i));
        }
    }
    return value;
}

static void pack_bytes(uint8_t *buf, int bit_pos, const uint8_t *src, int nbits) {
    for (int i = 0; i < nbits; i++) {
        int src_byte = i / 8;
        int src_bit = 7 - (i % 8);
        int dst_pos = bit_pos + i;
        int dst_byte = dst_pos / 8;
        int dst_bit = 7 - (dst_pos % 8);
        if (src[src_byte] & (1 << src_bit)) {
            buf[dst_byte] |= (1 << dst_bit);
        } else {
            buf[dst_byte] &= ~(1 << dst_bit);
        }
    }
}

static void payload_clear(uint8_t *buf) {
    memset(buf, 0, 10);
}

static void payload_set_i3(uint8_t *buf, uint8_t i3) {
    pack_bits(buf, 74, i3 & 0x07, 3);
}

static void payload_set_n3(uint8_t *buf, uint8_t n3) {
    pack_bits(buf, 71, n3 & 0x07, 3);
}

uint32_t encode_c28(const c28_t *c28) {
    switch (c28->type) {
        case C28_TYPE_DE:
            return 0;
        case C28_TYPE_QRZ:
            return 1;
        case C28_TYPE_CQ:
            return 2;
        case C28_TYPE_CQ_MOD: {
            int len = strlen(c28->payload.cq_modifier);
            if (len == 0) return 2;
            
            bool is_digits = true;
            for (int i = 0; i < len; i++) {
                if (c28->payload.cq_modifier[i] < '0' || c28->payload.cq_modifier[i] > '9') {
                    is_digits = false;
                    break;
                }
            }

            if (is_digits && len == 3) {
                uint32_t val = (c28->payload.cq_modifier[0] - '0') * 100 +
                               (c28->payload.cq_modifier[1] - '0') * 10 +
                               (c28->payload.cq_modifier[2] - '0');
                return 3 + val;
            } 

            else {
                uint32_t val = 0;
                for (int i = 0; i < len; i++) {
                    char c = c28->payload.cq_modifier[i];
                    
                    if (c >= 'a' && c <= 'z') c -= 32;
                    
                    uint32_t char_val = (c >= 'A' && c <= 'Z') ? (c - 'A' + 1) : 0;
                    val = (val * 27) + char_val;
                }
                return 1003 + val;
            }
        }
        
        case C28_TYPE_HASH_22: {
            return 2063592 + c28->payload.hash;
        }

        case C28_TYPE_CALLSIGN: {
            char call[7] = {0};
            int len = strlen(c28->payload.callsign);
            
            for(int i = 0; i < len && i < 6; i++) {
                char c = c28->payload.callsign[i];
                if (c >= 'a' && c <= 'z') c -= 32;
                call[i] = c;
            }

            int d_idx = -1;
            for (int i = 0; i < len && i <= 2; i++) {
                if (call[i] >= '0' && call[i] <= '9') {
                    d_idx = i;
                }
            }

            char std6[6] = {' ', ' ', ' ', ' ', ' ', ' '};
            if (d_idx >= 0) {
                int shift = 2 - d_idx;
                for (int i = 0; i < len; i++) {
                    if (i + shift < 6) {
                        std6[i + shift] = call[i];
                    }
                }
            }

            const char* a1 = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
            const char* a2 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
            const char* a3 = "0123456789";
            const char* a4 = " ABCDEFGHIJKLMNOPQRSTUVWXYZ";

            char *ptr;
            uint32_t i1 = (ptr = strchr(a1, std6[0])) ? (ptr - a1) : 0;
            uint32_t i2 = (ptr = strchr(a2, std6[1])) ? (ptr - a2) : 0;
            uint32_t i3 = (ptr = strchr(a3, std6[2])) ? (ptr - a3) : 0;
            uint32_t i4 = (ptr = strchr(a4, std6[3])) ? (ptr - a4) : 0;
            uint32_t i5 = (ptr = strchr(a4, std6[4])) ? (ptr - a4) : 0;
            uint32_t i6 = (ptr = strchr(a4, std6[5])) ? (ptr - a4) : 0;

            uint32_t n28 = 6257896; 
            n28 += i1 * 7085880;    
            n28 += i2 * 196830;     
            n28 += i3 * 19683;      
            n28 += i4 * 729;        
            n28 += i5 * 27;
            n28 += i6;

            return n28;
        }

        default:
            break;
    }

    return 0;
}

uint16_t encode_g15(const g15_t *g15) {
    if (!g15) return 0;
    
    uint16_t result = 0;

    if (g15->type == G15_TYPE_GRID) {
        for (int i = 0; i < 4; i++) {
            char c = g15->payload.grid[i];
            if (i < 2) {
                if (c < 'A' || c > 'Z') return 0;
            } else {
                if (c < '0' || c > '9') return 0;
            }
        }
    }

    switch (g15->type) {
        case G15_TYPE_GRID: {
            int j1 = (g15->payload.grid[0] - 'A') * 1800;
            int j2 = (g15->payload.grid[1] - 'A') * 100;
            int j3 = (g15->payload.grid[2] - '0') * 10;
            int j4 = (g15->payload.grid[3] - '0');
            
            result = (uint16_t)(j1 + j2 + j3 + j4);
            break;
        }
            
        case G15_TYPE_BLANK:
            result = MAXGRID4 + 1;
            break;
            
        case G15_TYPE_RRR:
            result = MAXGRID4 + 2;
            break;
            
        case G15_TYPE_RR73:
            result = MAXGRID4 + 3;
            break;
            
        case G15_TYPE_73:
            result = MAXGRID4 + 4;
            break;
            
        case G15_TYPE_REPORT:
            result = MAXGRID4 + g15->payload.report + 35;
            break;
            
        default:
            result = MAXGRID4 + 1;
            break;
    }

    return result & 0x7FFF;
}

uint32_t encode_g25(const char *grid) {
    if (!grid || strlen(grid) != 6) return 0;

    if (grid[0] < 'A' || grid[0] > 'R') return 0;
    if (grid[1] < 'A' || grid[1] > 'R') return 0;
    if (grid[2] < '0' || grid[2] > '9') return 0;
    if (grid[3] < '0' || grid[3] > '9') return 0;
    if (grid[4] < 'A' || grid[4] > 'X') return 0;
    if (grid[5] < 'A' || grid[5] > 'X') return 0;

    uint32_t j1 = (grid[0] - 'A') * 18 * 10 * 10 * 24 * 24;
    uint32_t j2 = (grid[1] - 'A') * 10 * 10 * 24 * 24;
    uint32_t j3 = (grid[2] - '0') * 10 * 24 * 24;
    uint32_t j4 = (grid[3] - '0') * 24 * 24;
    uint32_t j5 = (grid[4] - 'A') * 24;
    uint32_t j6 = (grid[5] - 'A');

    return j1 + j2 + j3 + j4 + j5 + j6;
}

void encode_f71(const char *text, uint8_t *output) {
    const char *charset = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-./?";

    char buf[14];
    memset(buf, ' ', 13);
    buf[13] = '\0';

    int len = text ? strlen(text) : 0;
    if (len > 13) len = 13;
    int offset = 13 - len;
    for (int i = 0; i < len; i++) {
        char c = text[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        buf[offset + i] = c;
    }

    uint8_t result[9] = {0};

    for (int i = 0; i < 13; i++) {
        const char *ptr = strchr(charset, buf[i]);
        uint8_t char_val = ptr ? (uint8_t)(ptr - charset) : 0;

        uint16_t carry = char_val;
        for (int j = 8; j >= 0; j--) {
            uint16_t prod = (uint16_t)result[j] * 42 + carry;
            result[j] = (uint8_t)(prod & 0xFF);
            carry = prod >> 8;
        }
    }

    memcpy(output, result, 9);
}

uint64_t encode_c58(const char *callsign) {
    const char *charset = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/";

    char buf[12];
    memset(buf, ' ', 11);
    buf[11] = '\0';

    int len = callsign ? strlen(callsign) : 0;
    if (len > 11) len = 11;
    for (int i = 0; i < len; i++) {
        char c = callsign[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        buf[i] = c;
    }

    uint64_t n58 = 0;
    for (int i = 0; i < 11; i++) {
        const char *ptr = strchr(charset, buf[i]);
        int char_val = ptr ? (int)(ptr - charset) : 0;
        n58 = n58 * 38 + char_val;
    }

    return n58;
}

int8_t encode_S7(const char *section) {
    static const char *sections[84] = {
        "AB",  "AK",  "AL",  "AR",  "AZ",  "BC",  "CO",  "CT",  "DE",  "EB",
        "EMA", "ENY", "EPA", "EWA", "GA",  "GTA", "IA",  "ID",  "IL",  "IN",
        "KS",  "KY",  "LA",  "LAX", "MAR", "MB",  "MDC", "ME",  "MI",  "MN",
        "MO",  "MS",  "MT",  "NC",  "ND",  "NE",  "NFL", "NH",  "NL",  "NLI",
        "NM",  "NNJ", "NNY", "NT",  "NTX", "NV",  "OH",  "OK",  "ONE", "ONN",
        "ONS", "OR",  "ORG", "PAC", "PR",  "QC",  "RI",  "SB",  "SC",  "SCV",
        "SD",  "SDG", "SF",  "SFL", "SJV", "SK",  "SNJ", "STX", "SV",  "TN",
        "UT",  "VA",  "VI",  "VT",  "WCF", "WI",  "WMA", "WNY", "WPA", "WTX",
        "WV",  "WWA", "WY",  "DX"
    };

    if (!section) return -1;

    char upper[4] = {0};
    int len = strlen(section);
    if (len > 3) return -1;
    for (int i = 0; i < len; i++) {
        char c = section[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        upper[i] = c;
    }

    for (int i = 0; i < 84; i++) {
        if (strcmp(upper, sections[i]) == 0) {
            return (int8_t)i;
        }
    }

    return -1;
}

uint16_t encode_s13(const s13_t *s13) {
    if (!s13) return 0;

    switch (s13->type) {
        case S13_TYPE_SERIAL:
            return (s13->payload.serial <= 7999) ? s13->payload.serial : 0;

        case S13_TYPE_STATE: {
            static const char *states[65] = {
                "AL",  "AK",  "AZ",  "AR",  "CA",  "CO",  "CT",  "DE",  "FL",  "GA",
                "HI",  "ID",  "IL",  "IN",  "IA",  "KS",  "KY",  "LA",  "ME",  "MD",
                "MA",  "MI",  "MN",  "MS",  "MO",  "MT",  "NE",  "NV",  "NH",  "NJ",
                "NM",  "NY",  "NC",  "ND",  "OH",  "OK",  "OR",  "PA",  "RI",  "SC",
                "SD",  "TN",  "TX",  "UT",  "VT",  "VA",  "WA",  "WV",  "WI",  "WY",
                "NB",  "NS",  "QC",  "ON",  "MB",  "SK",  "AB",  "BC",  "NWT", "NF",
                "LB",  "NU",  "YT",  "PEI", "DC"
            };

            char upper[4] = {0};
            int len = strlen(s13->payload.state);
            if (len > 3) return 0;
            for (int i = 0; i < len; i++) {
                char c = s13->payload.state[i];
                if (c >= 'a' && c <= 'z') c -= 32;
                upper[i] = c;
            }

            for (int i = 0; i < 65; i++) {
                if (strcmp(upper, states[i]) == 0) {
                    return 8001 + i;
                }
            }
            return 0;
        }

        default:
            return 0;
    }
}

uint32_t hash_callsign(const char *callsign, int nbits) {
    const char *charset = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/";
    const uint64_t nprime = 47055833459ULL;

    if (!callsign || (nbits != 10 && nbits != 12 && nbits != 22)) return 0;

    char buf[12];
    memset(buf, ' ', 11);
    buf[11] = '\0';

    int len = strlen(callsign);
    if (len > 11) len = 11;
    for (int i = 0; i < len; i++) {
        char c = callsign[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        buf[i] = c;
    }

    uint64_t n = 0;
    for (int i = 0; i < 11; i++) {
        const char *ptr = strchr(charset, buf[i]);
        int j = ptr ? (int)(ptr - charset) : 0;
        n = n * 38 + j;
    }

    return (uint32_t)((nprime * n) >> (64 - nbits));
}

uint8_t encode_r2(r2_t r2) {
    switch (r2) {
        case R2_TYPE_BLANK: return 0;
        case R2_TYPE_RRR:   return 1;
        case R2_TYPE_RR73:  return 2;
        case R2_TYPE_73:    return 3;
        default:            return 0;
    }
}

uint8_t encode_r3(int report) {
    if (report < 2 || report > 9) return 0;
    return (uint8_t)(report - 2);
}

uint8_t encode_r5(int report_db) {
    // r5: Values 0-31 convey signal reports -30, -28, ... +32 dB
    // (even numbers only)
    if (report_db < -30) report_db = -30;
    if (report_db > 32) report_db = 32;
    // Round to nearest even
    report_db = (report_db + 30) / 2;
    return (uint8_t)(report_db & 0x1F);
}

uint8_t encode_k3(k3_fd_class_t fd_class) {
    if (fd_class > FD_CLASS_F) return 0;
    return (uint8_t)fd_class;
}

uint16_t encode_s11(uint16_t serial) {
    return serial & 0x07FF;
}

void encode_t71(const char *hex, uint8_t *output) {

    memset(output, 0, 9);
    if (!hex) return;

    int len = strlen(hex);
    if (len > 18) len = 18;
    if (len == 0) return;

    uint8_t nibbles[18] = {0};
    for (int i = 0; i < len; i++) {
        char c = hex[i];
        if (c >= '0' && c <= '9')      nibbles[i] = c - '0';
        else if (c >= 'A' && c <= 'F') nibbles[i] = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') nibbles[i] = c - 'a' + 10;
        else                           nibbles[i] = 0;
    }

    if (len == 18 && nibbles[0] > 7) nibbles[0] = 7;

    uint8_t temp[9] = {0};
    int bit_pos = 72 - (len * 4);

    for (int i = 0; i < len; i++) {
        int byte_idx = (bit_pos + i * 4) / 8;
        int bit_off = (bit_pos + i * 4) % 8;

        if (bit_off <= 4) {
            temp[byte_idx] |= nibbles[i] << (4 - bit_off);
        } else {
            temp[byte_idx] |= nibbles[i] >> (bit_off - 4);
            if (byte_idx + 1 < 9) {
                temp[byte_idx + 1] |= nibbles[i] << (12 - bit_off);
            }
        }
    }

    temp[0] &= 0x7F;
    memcpy(output, temp, 9);
}

void encode_ftx_payload(const ftx_payload_t *payload, uint8_t *output) {
    payload_clear(output);
    switch (payload->type) {
        case FTX_MODE_FREE_TEXT: {
            uint8_t text_encoded[9] = {0};
            encode_f71(payload->data.free_text.text, text_encoded);
            pack_bytes(output, 0, text_encoded, 71);
            payload_set_i3(output, 0);
            payload_set_n3(output, 0);
            break;
        }
        case FTX_MODE_DXPEDITION: {
            uint32_t c28_0_encoded = encode_c28(&payload->data.dxpedition.c28_0);
            uint32_t c28_1_encoded = encode_c28(&payload->data.dxpedition.c28_1);
            uint16_t h10_encoded = payload->data.dxpedition.h10 & 0x3FF;
            uint8_t r5_encoded = encode_r5(payload->data.dxpedition.r5);

            pack_bits(output, 0, c28_0_encoded, 28);
            pack_bits(output, 28, c28_1_encoded, 28);
            pack_bits(output, 56, h10_encoded, 10);
            pack_bits(output, 66, r5_encoded, 5);
            payload_set_i3(output, 0);
            payload_set_n3(output, 1);
            break;
        }
        case FTX_MODE_FIELD_DAY: {
            uint32_t c28_0_encoded = encode_c28(&payload->data.field_day.c28_0);
            uint32_t c28_1_encoded = encode_c28(&payload->data.field_day.c28_1);
            uint8_t n4_encoded = payload->data.field_day.n4 & 0x0F;
            uint8_t k3_encoded = encode_k3(payload->data.field_day.k3) & 0x07;
            int8_t s7_val = encode_S7(payload->data.field_day.S7);
            uint8_t S7_encoded = (s7_val >= 0) ? (uint8_t)s7_val : 0;

            pack_bits(output, 0, c28_0_encoded, 28);
            pack_bits(output, 28, c28_1_encoded, 28);
            pack_bits(output, 56, payload->data.field_day.R1 ? 1 : 0, 1);
            pack_bits(output, 57, n4_encoded, 4);
            pack_bits(output, 61, k3_encoded, 3);
            pack_bits(output, 64, S7_encoded, 7);
            payload_set_i3(output, 0);
            payload_set_n3(output, (payload->data.field_day.transmitter_count > 16) ? 4 : 3);
            break;
        }
        case FTX_MODE_TELEMETRY: {
            pack_bytes(output, 0, payload->data.telemetry.data, 71);
            payload_set_i3(output, 0);
            payload_set_n3(output, 5);
            break;
        }
        case FTX_MODE_STD: {
            uint32_t c28_0_encoded = encode_c28(&payload->data.std.c28_0);
            uint32_t c28_1_encoded = encode_c28(&payload->data.std.c28_1);
            uint16_t g15_encoded = encode_g15(&payload->data.std.g15);

            pack_bits(output, 0, c28_0_encoded, 28);
            pack_bits(output, 28, payload->data.std.rover_suffix_0 ? 1 : 0, 1);
            pack_bits(output, 29, c28_1_encoded, 28);
            pack_bits(output, 57, payload->data.std.rover_suffix_1 ? 1 : 0, 1);
            pack_bits(output, 58, payload->data.std.R1 ? 1 : 0, 1);
            pack_bits(output, 59, g15_encoded, 15);
            payload_set_i3(output, 1);
            break;
        }
        case FTX_MODE_EU_VHF_2: {
            uint32_t c28_0_encoded = encode_c28(&payload->data.eu_vhf_2.c28_0);
            uint32_t c28_1_encoded = encode_c28(&payload->data.eu_vhf_2.c28_1);
            uint16_t g15_encoded = encode_g15(&payload->data.eu_vhf_2.g15);

            pack_bits(output, 0, c28_0_encoded, 28);
            pack_bits(output, 28, payload->data.eu_vhf_2.p_suffix_0 ? 1 : 0, 1);
            pack_bits(output, 29, c28_1_encoded, 28);
            pack_bits(output, 57, payload->data.eu_vhf_2.p_suffix_1 ? 1 : 0, 1);
            pack_bits(output, 58, payload->data.eu_vhf_2.R1 ? 1 : 0, 1);
            pack_bits(output, 59, g15_encoded, 15);
            payload_set_i3(output, 2);
            break;
        }
        case FTX_MODE_RTTY_RU: {
            // Type 3: t1 c28 c28 R1 r3 s13 i3
            uint32_t c28_0_encoded = encode_c28(&payload->data.rtty_ru.c28_0);
            uint32_t c28_1_encoded = encode_c28(&payload->data.rtty_ru.c28_1);
            uint8_t r3_encoded = encode_r3(payload->data.rtty_ru.r3);
            uint16_t s13_encoded = encode_s13(&payload->data.rtty_ru.s13);

            pack_bits(output, 0, payload->data.rtty_ru.t1 ? 1 : 0, 1);
            pack_bits(output, 1, c28_0_encoded, 28);
            pack_bits(output, 29, c28_1_encoded, 28);
            pack_bits(output, 57, payload->data.rtty_ru.R1 ? 1 : 0, 1);
            pack_bits(output, 58, r3_encoded, 3);
            pack_bits(output, 61, s13_encoded, 13);
            payload_set_i3(output, 3);
            break;
        }
        case FTX_MODE_NONSTD: {
            // Type 4: h12 c58 h1 r2 c1 i3
            uint16_t h12_encoded = payload->data.nonstd.h12 & 0x0FFF;
            uint64_t c58_encoded = encode_c58(payload->data.nonstd.c58);
            uint8_t r2_encoded = encode_r2(payload->data.nonstd.r2);

            pack_bits(output, 0, h12_encoded, 12);
            pack_bits(output, 12, c58_encoded, 58);
            pack_bits(output, 70, payload->data.nonstd.h1 ? 1 : 0, 1);
            pack_bits(output, 71, r2_encoded, 2);
            pack_bits(output, 73, payload->data.nonstd.c1 ? 1 : 0, 1);
            payload_set_i3(output, 4);
            break;
        }
        case FTX_MODE_EU_VHF_5: {
            // Type 5: h12 h22 R1 r3 s11 g25 i3
            uint16_t h12_encoded = payload->data.eu_vhf_5.h12 & 0x0FFF;
            uint32_t h22_encoded = payload->data.eu_vhf_5.h22 & 0x3FFFFF;
            uint8_t r3_encoded = encode_r3(payload->data.eu_vhf_5.r3);
            uint16_t s11_encoded = encode_s11(payload->data.eu_vhf_5.s11);
            uint32_t g25_encoded = encode_g25(payload->data.eu_vhf_5.g25);

            pack_bits(output, 0, h12_encoded, 12);
            pack_bits(output, 12, h22_encoded, 22);
            pack_bits(output, 34, payload->data.eu_vhf_5.R1 ? 1 : 0, 1);
            pack_bits(output, 35, r3_encoded, 3);
            pack_bits(output, 38, s11_encoded, 11);
            pack_bits(output, 49, g25_encoded, 25);
            payload_set_i3(output, 5);
            break;
        }
        default:
            break;
    }
}
