#include "protocol/cobs.h"

size_t cobs_encode(const uint8_t *input, size_t length, uint8_t *output) {
    size_t read_index = 0;
    size_t write_index = 1;
    size_t code_index = 0;
    uint8_t code = 1;
    while (read_index < length) {
        if (input[read_index] == 0) {
            output[code_index] = code;
            code = 1;
            code_index = write_index;
            write_index++;
            read_index++;
        } else {
            output[write_index++] = input[read_index++];
            code++;

            if (code == 0xFF) {
                output[code_index] = code;
                code = 1;
                code_index = write_index;
                write_index++;
            }
        }
    }

    output[code_index] = code;

    return write_index;
}

size_t cobs_decode(const uint8_t *input, size_t length, uint8_t *output) {
    size_t read_index = 0;
    size_t write_index = 0;
    uint8_t code;
    uint8_t i;

    while (read_index < length) {
        code = input[read_index];

        if (code == 0) {
            return 0; 
        }

        read_index++;

        for (i = 1; i < code; i++) {
            if (read_index >= length) {
                return 0;
            }
            output[write_index++] = input[read_index++];
        }

        if (code < 0xFF && read_index < length) {
            output[write_index++] = 0;
        }
    }

    return write_index;
}
