#ifndef HARDWARE_OLED_H
#define HARDWARE_OLED_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

int init_oled(void);
int oled_print(const char *str, uint16_t x, uint16_t y);
int oled_clear(void);
int oled_flush(void);
int oled_set_pixel(uint16_t x, uint16_t y, bool on);
int oled_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

extern char _oled_print_buf[128];

#define oled_printf(x, y, fmt, ...) do {                                    \
    snprintf(_oled_print_buf, sizeof(_oled_print_buf), fmt, ##__VA_ARGS__); \
    oled_print(_oled_print_buf, x, y);                                      \
} while (0)

#endif /* HARDWARE_OLED_H */
