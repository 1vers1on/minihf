#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/regulator.h>
#include "drivers/clock_control/clock_si5351a.h"
#include "config.h"
#include "radio/radio_cmd.h"
#include "stm32l431xx.h"
#include "radio/tx_engine.h"
#include "uart_handler.h"
#include <zephyr/drivers/gpio.h>
#include <stm32l4xx.h>
#include <stdio.h>
#include "hardware/tr_switch.h"
#include <zephyr/drivers/display.h>
#include <zephyr/display/cfb.h>
#include "hardware/oled.h"

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

char dbg_buf[256];

static int regulator_init() {
    debug_printf("[REG] Starting regulator init");
    int tries = 0;
    while (!device_is_ready(regulator)) {
        debug_printf("[REG] Waiting for regulator (attempt %d/%d)", tries + 1, REGULATOR_TRY_COUNT);
        k_sleep(K_SECONDS(1));
        tries++;
        if (tries > REGULATOR_TRY_COUNT) {
            debug_printf("[REG] Regulator not ready after %d attempts", REGULATOR_TRY_COUNT);
            return -1;
        }
    }
    debug_printf("[REG] Regulator ready, setting voltage to 1.2V");
    int ret = regulator_set_voltage(regulator, 1200000, 1200000);
    debug_printf("[REG] set_voltage returned %d", ret);
    ret = regulator_disable(regulator);
    debug_printf("[REG] disable returned %d", ret);

    debug_printf("[REG] Regulator init complete");
    return 0;
}

static int init_si5351a() {
    debug_printf("[SI5351A] Starting init");
    int tries = 0;
    while (!device_is_ready(si5351a)) {
        debug_printf("[SI5351A] Waiting for device (attempt %d/%d)", tries + 1, SI5351A_TRY_COUNT);
        k_sleep(K_SECONDS(1));
        tries++;
        if (tries > SI5351A_TRY_COUNT) {
            debug_printf("[SI5351A] Device not ready after %d attempts", SI5351A_TRY_COUNT);
            return -1;
        }
    }
    int ret;
    k_msleep(500); // give it a moment to power up
    debug_printf("[SI5351A] setting pll registers");
    ret = si5351a_set_pll_freq(si5351a, 'A', 600000000);
    if (ret) {
        debug_printf("[SI5351A] Failed to set PLLA registers");
        return ret;
    }
    k_msleep(500);
    ret = si5351a_set_ms_freq(si5351a, 0, 500000, 0, 'A');
    if (ret) {
        debug_printf("[SI5351A] Failed to set multisynth registers");
        return ret;
    }
    ret = si5351a_reset_pll(si5351a, true, true);
    if (ret) {
        debug_printf("[SI5351A] Failed to reset PLL");
        return ret;
    }
    ret = si5351a_enable_output(si5351a, 0, true);
    if (ret) {
        debug_printf("[SI5351A] Failed to enable output");
        return ret;
    }
    return 0;
}

static int init_rtc() {
    debug_printf("[RTC] Starting RTC init");
    int tries = 0;
    while (!device_is_ready(rtc_dev)) {
        debug_printf("[RTC] Waiting for device (attempt %d/%d)", tries + 1, RTC_TRY_COUNT);
        k_sleep(K_SECONDS(1));
        tries++;
        if (tries > RTC_TRY_COUNT) {
            debug_printf("[RTC] Device not ready after %d attempts", RTC_TRY_COUNT);
            return -1;
        }
    }
    debug_printf("[RTC] Device ready, init complete");

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

    uart_handler_init();

    debug_printf("=== minihf boot ===");

    if (init_si5351a() < 0) {
        debug_printf("[MAIN] SI5351A init failed, aborting");
        return -1;
    }

    if (regulator_init() < 0) {
        debug_printf("[MAIN] Regulator init failed, continuing without it");
    }

    if (init_rtc() < 0) {
        debug_printf("[MAIN] RTC init failed, aborting");
        return -1;
    }

    if (tr_switch_init() < 0) {
        debug_printf("[MAIN] TR switch init failed, aborting");
        return -1;
    }

    if (init_oled() < 0) {
        debug_printf("[MAIN] OLED init failed, continuing without it");
    }

    debug_printf("[MAIN] Initializing TX engine");
    tx_engine_init();

    debug_printf("[MAIN] Configuring LEDs");
    gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led3, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led4, GPIO_OUTPUT_INACTIVE);

    gpio_pin_set_dt(&led1, 0);
    gpio_pin_set_dt(&led2, 0);
    gpio_pin_set_dt(&led3, 0);
    gpio_pin_set_dt(&led4, 0);

    debug_printf("[MAIN] Init complete, entering main loop");

    while (1) {
        gpio_pin_toggle_dt(&led1);
        gpio_pin_toggle_dt(&led2);
        gpio_pin_toggle_dt(&led3);
        gpio_pin_toggle_dt(&led4);

        si5351a_update_status(si5351a);
        struct si5351a_data *clk_data = (struct si5351a_data *)si5351a->data;
        debug_printf("[CLK] SYS_INIT=%u LOL_A=%u LOL_B=%u LOS=%u REV=%u",
                     clk_data->dev_status.SYS_INIT,
                     clk_data->dev_status.LOL_A,
                     clk_data->dev_status.LOL_B,
                     clk_data->dev_status.LOS,
                     clk_data->dev_status.REVID);
        
        k_sleep(K_SECONDS(1));
    }

    return 0;
}
