#ifndef SRC_CONFIG_H
#define SRC_CONFIG_H

#include <zephyr/device.h>

#define REGULATOR_TRY_COUNT 5
#define SI5351A_TRY_COUNT 5
#define RTC_TRY_COUNT 5
#define OLED_TRY_COUNT 5

extern const struct device *regulator;
extern const struct device *si5351a;
extern const struct device *uart_dev;
extern const struct device *rtc_dev;
extern const struct device *oled_dev;

#endif // SRC_CONFIG_H
