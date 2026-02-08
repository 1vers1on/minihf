#ifndef ZEPHYR_DRIVERS_CLOCK_CONTROL_SI5351A_H_
#define ZEPHYR_DRIVERS_CLOCK_CONTROL_SI5351A_H_

#include <zephyr/drivers/i2c.h>
#include <zephyr/device.h>
#include <stdint.h>
#include <stdbool.h>

#define SI5351A_NUM_OUTPUTS 3

#define SI5351A_FREQ_MULT 100

struct si5351a_config {
    struct i2c_dt_spec i2c;
    uint32_t xtal_freq;
    uint32_t crystal_load_capacitance;
};

struct si5351a_data {
    uint8_t clk_enabled;
    uint64_t plla_freq;
    uint64_t pllb_freq;
    uint64_t output_freq[SI5351A_NUM_OUTPUTS];
    uint8_t pll_assignments[SI5351A_NUM_OUTPUTS];
};

struct si5351a_reg_set {
    uint32_t p1;
    uint32_t p2;
    uint32_t p3;
};

uint64_t si5351a_calc_pll(uint8_t target_pll, uint64_t freq, uint32_t xtal_freq, struct si5351a_reg_set *reg_set);
void si5351a_set_pll(const struct device *dev, uint8_t target_pll, uint64_t pll_freq);

void si5351a_set_multisynth(const struct device *dev, uint8_t synth, struct si5351a_reg_set reg_set, uint8_t mode, uint8_t rdiv, uint8_t div_by_4);

void si5351a_set_ms_source(const struct device *dev, uint8_t clk, uint8_t pll);
void si5351a_output_enable(const struct device *dev, uint8_t clk, uint8_t enable);
void si5351a_set_phase(const struct device *dev, uint8_t clk, uint8_t phase);
uint8_t si5351a_set_freq(const struct device *dev, uint8_t output, uint64_t freq);

#endif /* ZEPHYR_DRIVERS_CLOCK_CONTROL_SI5351A_H_ */
