#include <zephyr/drivers/gpio.h>
#include "hardware/tr_switch.h"
#include "radio/radio.h"

static const struct gpio_dt_spec tr_fwd = GPIO_DT_SPEC_GET(DT_NODELABEL(tr_forward), gpios);
static const struct gpio_dt_spec tr_bwd = GPIO_DT_SPEC_GET(DT_NODELABEL(tr_backward), gpios);

int tr_switch_init() {
    if (!gpio_is_ready_dt(&tr_fwd) || !gpio_is_ready_dt(&tr_bwd)) {
        return -ENODEV;
    }

    gpio_pin_configure_dt(&tr_fwd, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&tr_bwd, GPIO_OUTPUT_INACTIVE);

    gpio_pin_set_dt(&tr_fwd, 0);
    gpio_pin_set_dt(&tr_bwd, 1);
    tx_active = false;

    return 0;
}

void tr_set_tx() {
    gpio_pin_set_dt(&tr_fwd, 1);
    gpio_pin_set_dt(&tr_bwd, 0);
    tx_active = true;
}

void tr_set_rx() {
    gpio_pin_set_dt(&tr_fwd, 0);
    gpio_pin_set_dt(&tr_bwd, 1);
    tx_active = false;
}
