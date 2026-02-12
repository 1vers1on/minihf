#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/regulator.h>
#include "drivers/clock_control/clock_si5351a.h"
#include "config.h"
#include "uart_handler.h"

const struct device *regulator = DEVICE_DT_GET(DT_NODELABEL(tps55287));
const struct device *si5351a = DEVICE_DT_GET(DT_NODELABEL(si5351a));
const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(lpuart1));

static int regulator_init() {
    int tries = 0;
    while (!device_is_ready(regulator)) {
        printk("Waiting for regulator device to be ready...\n");
        k_sleep(K_SECONDS(1));
        tries++;
        if (tries > REGULATOR_TRY_COUNT) {
            printk("Regulator device not ready after 5 seconds, giving up.\n");
            return -1;
        }
    }
    regulator_enable(regulator);

    return 0;
}

static int init_si5351a() {
    int tries = 0;
    while (!device_is_ready(si5351a)) {
        printk("Waiting for SI5351A device to be ready...\n");
        k_sleep(K_SECONDS(1));
        tries++;
        if (tries > SI5351A_TRY_COUNT) {
            printk("SI5351A device not ready after 5 seconds, giving up.\n");
            return -1;
        }
    }

    si5351a_output_enable(si5351a, 0, false);
    si5351a_output_enable(si5351a, 1, false);
    si5351a_output_enable(si5351a, 2, false);

    si5351a_set_ms_source(si5351a, 0, 0);
    si5351a_set_ms_source(si5351a, 1, 0);
    si5351a_set_ms_source(si5351a, 2, 0);

    return 0;
}

int main(void) {
    printk("hello from my stm32l4 board\n");

    if (init_si5351a() < 0) {
        return -1;
    }

    if (regulator_init() < 0) {
        return -1;
    }

    uart_handler_init();

    while (1) {
        k_sleep(K_SECONDS(1));
        printk("still alive\n");
    }

    return 0;
}
