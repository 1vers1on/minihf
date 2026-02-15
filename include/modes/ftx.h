#ifndef MODES_ENCODERS_FTX_H
#define MODES_ENCODERS_FTX_H

#include <stdint.h>
#include <sys/_intsup.h>
#include <zephyr/kernel.h>

// util for encoding ft8 and related modes.

typedef enum {
    FTX_MODE_FREE_TEXT,     // Type 0.0
    FTX_MODE_DXPEDITION,    // Type 0.1
    FTX_MODE_FIELD_DAY,     // Type 0.3 / 0.4
    FTX_MODE_TELEMETRY,     // Type 0.5
    FTX_MODE_STD,           // Type 1
    FTX_MODE_EU_VHF_2,      // Type 2
    FTX_MODE_RTTY_RU,       // Type 3
    FTX_MODE_NONSTD,        // Type 4
    FTX_MODE_EU_VHF_5       // Type 5 (Added this missing type!)
} ftx_payload_type_t;

typedef enum {
    C28_TYPE_CQ,          // Standard CQ (no modifier)
    C28_TYPE_CQ_MOD,      // CQ with a modifier (e.g., DX, POTA, 090)
    C28_TYPE_DE,
    C28_TYPE_QRZ,
    C28_TYPE_CALLSIGN,
    C28_TYPE_HASH_22
} c28_type_t;

typedef enum {
    G15_TYPE_GRID,
    G15_TYPE_REPORT,
    G15_TYPE_RRR,
    G15_TYPE_RR73,
    G15_TYPE_73,
    G15_TYPE_BLANK
} g15_type_t;

typedef enum {
    S13_TYPE_SERIAL,
    S13_TYPE_STATE
} s13_type_t;

typedef enum {
    R2_TYPE_RRR,
    R2_TYPE_RR73,
    R2_TYPE_73,
    R2_TYPE_BLANK
} r2_t;

typedef enum {
    FD_CLASS_A,
    FD_CLASS_B,
    FD_CLASS_C,
    FD_CLASS_D,
    FD_CLASS_E,
    FD_CLASS_F
} k3_fd_class_t;

typedef struct {
    c28_type_t type;
    
    union {
        char callsign[7];    // Standard callsign (up to 6 chars + '\0')
        uint32_t hash;       // 22-bit hash
        char cq_modifier[5]; // 1 to 4 letters, or 3 digits + '\0' (e.g., "DX", "POTA", "090")
    } payload;
} c28_t;

typedef struct {
    g15_type_t type;
    union {
        char grid[5];    // 4-character grid + null terminator
        int8_t report;   // Signal report value (if replacing the grid)
    } payload;
} g15_t;

typedef struct {
    s13_type_t type;
    union {
        uint16_t serial; // Serial number (0 to 7999)
        char state[3];   // State/Province abbr + null terminator
    } payload;
} s13_t;

typedef struct {
    ftx_payload_type_t type;
    
    union {
        struct {
            char text[14]; // f71: 13 chars + null terminator
        } free_text;

        struct {
            c28_t c28_0;
            c28_t c28_1;
            uint16_t h10; // 10-bit callsign hash
            int8_t r5;    // Report: -30 to +32, even numbers only
        } dxpedition;

        struct {
            c28_t c28_0;
            c28_t c28_1;
            bool R1;               // "R" (Roger/Receipt) acknowledgment flag
            uint8_t n4;            // 4-bit field: Number of transmitters
            k3_fd_class_t k3;      // 3-bit field: Field Day Class (A-F)
            char S7[4];      // Section as text (e.g., "CT", "ON", "DX") - will be mapped to 7-bit code
            uint8_t transmitter_count; // Actual number of transmitters (for n3 calculation)
        } field_day;

        struct {
            uint8_t data[9]; // t71: 71 bits of telemetry fits in 9 bytes
        } telemetry;

        struct {
            c28_t c28_0;
            bool rover_suffix_0; // r1: If true, append "/R" to callsign
            c28_t c28_1;
            bool rover_suffix_1; // r1: If true, append "/R" to callsign
            bool R1;             // "R" (Roger/Receipt) flag
            g15_t g15;
        } std;

        struct {
            c28_t c28_0;
            bool p_suffix_0; // p1: If true, append "/P" to callsign
            c28_t c28_1;
            bool p_suffix_1; // p1: If true, append "/P" to callsign
            bool R1;         // "R" (Roger/Receipt) flag
            g15_t g15;
        } eu_vhf_2;

        struct {
            bool t1;         // "TU;" (Thank you) flag
            c28_t c28_0;
            c28_t c28_1;
            bool R1;         // "R" (Roger/Receipt) flag
            uint8_t r3;      // Report: 2-9
            s13_t s13;       // Serial number or State
        } rtty_ru;

        struct {
            uint16_t h12; // 12-bit hashed callsign
            char c58[12]; // 11 char non-standard callsign + null terminator
            bool h1;      // Flag: hashed callsign is second callsign
            r2_t r2;      // RRR, RR73, 73, or blank
            bool c1;      // Flag: first callsign is CQ, h12 is ignored
        } nonstd;

        struct {
            uint16_t h12; // 12-bit hashed callsign
            uint32_t h22; // 22-bit hashed callsign
            bool R1;      // "R" (Roger/Receipt) flag
            uint8_t r3;   // Report: 2-9
            uint16_t s11; // Serial number (0-2047)
            char g25[7];  // 6-character grid + null terminator
        } eu_vhf_5;
        
    } data;
} ftx_payload_t;

uint32_t encode_c28(const c28_t *c28);
uint16_t encode_g15(const g15_t *g15);
uint32_t encode_g25(const char *grid);
void encode_f71(const char *text, uint8_t *output);
uint64_t encode_c58(const char *callsign);
int8_t encode_S7(const char *section);
uint16_t encode_s13(const s13_t *s13);
uint32_t hash_callsign(const char *callsign, int nbits);
uint8_t encode_r2(r2_t r2);
uint8_t encode_r3(int report);
uint8_t encode_r5(int report_db);
uint8_t encode_k3(k3_fd_class_t fd_class);
uint16_t encode_s11(uint16_t serial);
void encode_t71(const char *hex, uint8_t *output);

void encode_ftx_payload(const ftx_payload_t *payload, uint8_t *output);

#endif // MODES_ENCODERS_FTX_H
