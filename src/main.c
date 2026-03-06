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

static char dbg_buf[256];
#define debug_printf(fmt, ...) do { \
    snprintf(dbg_buf, sizeof(dbg_buf), fmt, ##__VA_ARGS__); \
    send_debug_message(dbg_buf); \
} while(0)

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
    debug_printf("[SI5351A] Device ready");

    debug_printf("[SI5351A] Disabling all outputs");
    si5351a_output_enable(si5351a, 0, false);
    si5351a_output_enable(si5351a, 1, false);
    si5351a_output_enable(si5351a, 2, false);

    debug_printf("[SI5351A] Setting MS sources to PLL A");
    si5351a_set_ms_source(si5351a, 0, 0);
    si5351a_set_ms_source(si5351a, 1, 0);
    si5351a_set_ms_source(si5351a, 2, 0);

    uint8_t ret;
    debug_printf("[SI5351A] Setting CLK0 to 100 MHz");
    ret = si5351a_set_freq(si5351a, 0, 100000000);
    debug_printf("[SI5351A] CLK0 set_freq returned %u", ret);
    si5351a_output_enable(si5351a, 0, true);

    debug_printf("[SI5351A] Setting CLK1 to 100 MHz");
    ret = si5351a_set_freq(si5351a, 1, 100000000);
    debug_printf("[SI5351A] CLK1 set_freq returned %u", ret);
    si5351a_output_enable(si5351a, 1, true);

    debug_printf("[SI5351A] Setting CLK2 to 100 MHz");
    ret = si5351a_set_freq(si5351a, 2, 100000000);
    debug_printf("[SI5351A] CLK2 set_freq returned %u", ret);
    si5351a_output_enable(si5351a, 2, true);
    
    si5351a_set_clock_pwr(si5351a, 0, 1);
    si5351a_set_clock_pwr(si5351a, 1, 1);
    si5351a_set_clock_pwr(si5351a, 2, 1);

    si5351a_set_pll(si5351a, 0, 80000000000ULL);
    si5351a_set_pll(si5351a, 1, 80000000000ULL);

    debug_printf("[SI5351A] All outputs enabled, init complete");
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

    // init UART first so we can use send_debug_message everywhere
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

        // read and report SI5351A status
        si5351a_update_status(si5351a);
        struct si5351a_data *clk_data = (struct si5351a_data *)si5351a->data;
        debug_printf("[CLK] SYS_INIT=%u LOL_A=%u LOL_B=%u LOS=%u REV=%u",
                     clk_data->dev_status.SYS_INIT,
                     clk_data->dev_status.LOL_A,
                     clk_data->dev_status.LOL_B,
                     clk_data->dev_status.LOS,
                     clk_data->dev_status.REVID);
        debug_printf("[CLK] PLLA=%llu PLLB=%llu clk0=%llu clk1=%llu clk2=%llu",
                     clk_data->plla_freq,
                     clk_data->pllb_freq,
                     clk_data->output_freq[0],
                     clk_data->output_freq[1],
                     clk_data->output_freq[2]);

        k_sleep(K_SECONDS(1));
    }

    return 0;
}
