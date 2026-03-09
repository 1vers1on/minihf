#include "radio/radio.h"
#include <stdbool.h>

uint64_t base_frequency = 10136000 * 100; // 100 to avoid floating point issues
bool tx_active = false;
