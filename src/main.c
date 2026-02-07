#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
const struct device *si5351a = DEVICE_DT_GET(DT_NODELABEL(si5351a));

int main(void) {
    printk("hello from my stm32l4 board\n");

    while (1) {
        k_sleep(K_SECONDS(1));
        printk("still alive\n");
    }

    return 0;
}
