#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/regulator.h>
#include "drivers/clock_control/clock_si5351a.h"
#include "config.h"
#include "stm32l431xx.h"
#include "uart_handler.h"
#include <zephyr/drivers/gpio.h>
#include <stm32l4xx.h>

const struct device *regulator = DEVICE_DT_GET(DT_NODELABEL(tps55289));
const struct device *si5351a = DEVICE_DT_GET(DT_NODELABEL(si5351a));
const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(lpuart1));
const struct device *rtc_dev = DEVICE_DT_GET(DT_NODELABEL(rtc));

#define LED1_NODE DT_NODELABEL(led1)
#define LED2_NODE DT_NODELABEL(led2)
#define LED3_NODE DT_NODELABEL(led3)
#define LED4_NODE DT_NODELABEL(led4)

static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(LED2_NODE, gpios);
static const struct gpio_dt_spec led3 = GPIO_DT_SPEC_GET(LED3_NODE, gpios);
static const struct gpio_dt_spec led4 = GPIO_DT_SPEC_GET(LED4_NODE, gpios);

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
    regulator_disable(regulator);

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

static int init_rtc() {
    int tries = 0;
    while (!device_is_ready(rtc_dev)) {
        printk("Waiting for RTC device to be ready...\n");
        k_sleep(K_SECONDS(1));
        tries++;
        if (tries > RTC_TRY_COUNT) {
            printk("RTC device not ready after %d seconds, giving up.\n", RTC_TRY_COUNT);
            return -1;
        }
    }

    return 0;
}

void enable_debug_in_pm() {
    DBGMCU->CR |= DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STOP | DBGMCU_CR_DBG_STANDBY;
    DBGMCU->APB1FZR1 |= DBGMCU_APB1FZR1_DBG_IWDG_STOP 
                     | DBGMCU_APB1FZR1_DBG_WWDG_STOP 
                     | DBGMCU_APB1FZR1_DBG_RTC_STOP;
}

int main(void) {
    k_busy_wait(2000000);

    enable_debug_in_pm();

    printk("hello\n");

    // if (init_si5351a() < 0) {
    //     return -1;
    // }

    if (regulator_init() < 0) {
        printk("Failed to initialize regulator, continuing without it.\n");
    }

    if (init_rtc() < 0) {
        return -1;
    }

    uart_handler_init();

    gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led3, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led4, GPIO_OUTPUT_INACTIVE);

    gpio_pin_set_dt(&led1, 0);
    gpio_pin_set_dt(&led2, 0);
    gpio_pin_set_dt(&led3, 0);
    gpio_pin_set_dt(&led4, 0);

    while (1) {
        gpio_pin_toggle_dt(&led1);
        gpio_pin_toggle_dt(&led2);
        gpio_pin_toggle_dt(&led3);
        gpio_pin_toggle_dt(&led4);
        k_sleep(K_SECONDS(1));
    }

    return 0;
}
