#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include "drivers/clock_control/clock_si5351a.h"

const struct device *si5351a = DEVICE_DT_GET(DT_NODELABEL(si5351a));

int main(void) {
    printk("hello from my stm32l4 board\n");

    if (!device_is_ready(si5351a)) {
        printk("SI5351A device not ready\n");
        return -1;
    }

    si5351a_set_ms_source(si5351a, 0, 0);
    si5351a_set_freq(si5351a, 0, 7000000ULL * SI5351A_FREQ_MULT);
    si5351a_output_enable(si5351a, 0, 1);

    while (1) {
        k_sleep(K_SECONDS(1));
        printk("still alive\n");
    }

    return 0;
}
