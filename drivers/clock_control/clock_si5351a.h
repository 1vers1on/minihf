#include "config.h"
#include <zephyr/drivers/i2c.h>
#include <zephyr/device.h>
#include <stdint.h>
#include <stdbool.h>

struct si5351a_config {
    struct i2c_dt_spec i2c;
    uint32_t xtal_freq;
    uint32_t crystal_load_capacitance;
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

struct si5351a_data {
    bool initialised;
    bool plla_configured;
    uint32_t plla_freq;
    bool pllb_configured;
    uint32_t pllb_freq;
    struct si5351a_status dev_status;
    struct si5351a_int_status dev_int_status;
};

struct si5351a_multisynth_config {
    uint32_t P1;
    uint32_t P2;
    uint32_t P3;
};

int si5351a_write_reg(const struct device *dev, uint8_t reg, uint8_t value);
int si5351a_write_multiple(const struct device *dev, uint8_t start_reg, uint8_t *values, size_t length);
int si5351a_read_reg(const struct device *dev, uint8_t reg, uint8_t *value);

int si5351a_enable_spread_spectrum(const struct device *dev, bool enable);
int si5351a_update_sys_status(const struct device *dev);
int si5351a_update_int_status(const struct device *dev);
int si5351a_update_status(const struct device *dev);
int si5351a_reset_pll(const struct device *dev, bool reset_a, bool reset_b);
int si5351a_set_pll(const struct device *dev, char pll, uint32_t a, uint32_t b, uint32_t c);
int si5351a_set_pll_freq(const struct device *dev, char pll, uint32_t freq);
int si5351a_set_ms(const struct device *dev, uint8_t ms, uint32_t a, uint32_t b, uint32_t c, char pll);
int si5351a_set_ms_freq(const struct device *dev, uint8_t ms,
                       uint32_t freq_hz, uint32_t freq_millihz, char pll);
int si5351a_enable_output(const struct device *dev, uint8_t output, bool enable);