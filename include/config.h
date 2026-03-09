#ifndef SRC_CONFIG_H
#define SRC_CONFIG_H

#include <zephyr/device.h>
#include <zephyr/sys/printk.h>
#include "radio/radio_cmd.h"

#define REGULATOR_TRY_COUNT 5
#define SI5351A_TRY_COUNT 5
#define RTC_TRY_COUNT 5
#define OLED_TRY_COUNT 1

#define FONT 0

extern const struct device *regulator;
extern const struct device *si5351a;
extern const struct device *uart_dev;
extern const struct device *rtc_dev;

extern char dbg_buf[256];
#define debug_printf(fmt, ...) do { \
    snprintf(dbg_buf, sizeof(dbg_buf), fmt, ##__VA_ARGS__); \
    send_debug_message(dbg_buf); \
} while(0)

#endif // SRC_CONFIG_H
