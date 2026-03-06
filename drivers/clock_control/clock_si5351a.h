#ifndef ZEPHYR_DRIVERS_CLOCK_CONTROL_SI5351A_H_
#define ZEPHYR_DRIVERS_CLOCK_CONTROL_SI5351A_H_

#include <zephyr/drivers/i2c.h>
#include <zephyr/device.h>
#include <stdint.h>
#include <stdbool.h>

#define SI5351A_NUM_OUTPUTS 3

#define SI5351A_FREQ_MULT 100

enum si5351a_drive {
    SI5351A_DRIVE_2MA = 0,
    SI5351A_DRIVE_4MA = 1,
    SI5351A_DRIVE_6MA = 2,
    SI5351A_DRIVE_8MA = 3,
};

enum si5351a_clock_source {
    SI5351A_CLK_SRC_XTAL = 0,
    SI5351A_CLK_SRC_CLKIN = 1,
    SI5351A_CLK_SRC_MS0 = 2,
    SI5351A_CLK_SRC_MS = 3,
};

enum si5351a_clock_disable {
    SI5351A_CLK_DISABLE_LOW = 0,
    SI5351A_CLK_DISABLE_HIGH = 1,
    SI5351A_CLK_DISABLE_HI_Z = 2,
    SI5351A_CLK_DISABLE_NEVER = 3,
};

enum si5351a_clock_fanout {
    SI5351A_FANOUT_CLKIN = 0,
    SI5351A_FANOUT_XO = 1,
    SI5351A_FANOUT_MS = 2,
};

enum si5351a_pll_input {
    SI5351A_PLL_INPUT_XO = 0,
    SI5351A_PLL_INPUT_CLKIN = 1,
};

struct si5351a_status {
    uint8_t SYS_INIT;
    uint8_t LOL_B;
    uint8_t LOL_A;
    uint8_t LOS;
    uint8_t REVID;
};

struct si5351a_int_status {
    uint8_t SYS_INIT_STKY;
    uint8_t LOL_B_STKY;
    uint8_t LOL_A_STKY;
    uint8_t LOS_STKY;
};

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
    int32_t ref_correction[2];
    enum si5351a_pll_input plla_ref_osc;
    enum si5351a_pll_input pllb_ref_osc;
    struct si5351a_status dev_status;
    struct si5351a_int_status dev_int_status;
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

void si5351a_drive_strength(const struct device *dev, uint8_t clk, enum si5351a_drive drive);
void si5351a_set_clock_pwr(const struct device *dev, uint8_t clk, uint8_t pwr);
void si5351a_set_clock_invert(const struct device *dev, uint8_t clk, uint8_t inv);
void si5351a_set_clock_source(const struct device *dev, uint8_t clk, enum si5351a_clock_source src);
void si5351a_set_clock_disable(const struct device *dev, uint8_t clk, enum si5351a_clock_disable dis_state);
void si5351a_set_clock_fanout(const struct device *dev, enum si5351a_clock_fanout fanout, uint8_t enable);
void si5351a_set_pll_input(const struct device *dev, uint8_t pll, enum si5351a_pll_input input);
uint8_t si5351a_set_freq_manual(const struct device *dev, uint8_t output, uint64_t freq, uint64_t pll_freq);
void si5351a_set_correction(const struct device *dev, int32_t corr, enum si5351a_pll_input ref_osc);
int32_t si5351a_get_correction(const struct device *dev, enum si5351a_pll_input ref_osc);
void si5351a_update_sys_status(const struct device *dev);
void si5351a_update_int_status(const struct device *dev);
void si5351a_update_status(const struct device *dev);

#endif /* ZEPHYR_DRIVERS_CLOCK_CONTROL_SI5351A_H_ */
