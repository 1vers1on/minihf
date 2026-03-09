#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include "clock_si5351a.h"
#include "config.h"
#include <zephyr/drivers/clock_control.h>
#include <math.h>

#define si5351a_PLL_VCO_MIN 600000000ULL
#define si5351a_PLL_VCO_MAX 900000000ULL

#define si5351a_MULTISYNTH_MIN_FREQ      500000ULL
#define si5351a_MULTISYNTH_DIVBY4_FREQ   150000000ULL
#define si5351a_MULTISYNTH_MAX_FREQ      200000000ULL
#define si5351a_MULTISYNTH_SHARE_MAX     100000000ULL

#define si5351a_PLL_FIXED                80000000000ULL

#define si5351a_CRYSTAL_LOAD 183
#define si5351a_CRYSTAL_LOAD_MASK        (3<<6)
#define si5351a_CRYSTAL_LOAD_0PF         (0<<6)
#define si5351a_CRYSTAL_LOAD_6PF         (1<<6)
#define si5351a_CRYSTAL_LOAD_8PF         (2<<6)
#define si5351a_CRYSTAL_LOAD_10PF        (3<<6)

#define si5351a_PLL_A_MIN                15
#define si5351a_PLL_A_MAX                90
#define si5351a_PLL_B_MAX                1048574
#define RFRAC_DENOM 1000000ULL

#define si5351a_PLLA_PARAMETERS          26
#define si5351a_PLLB_PARAMETERS          34
#define si5351a_CLK0_PARAMETERS          42
#define si5351a_CLK1_PARAMETERS          50
#define si5351a_CLK2_PARAMETERS          58

#define si5351a_CLK0_CTRL                16
#define si5351a_CLK1_CTRL                17
#define si5351a_CLK2_CTRL                18
#define si5351a_CLK_PLL_SELECT           (1<<5)
#define si5351a_OUTPUT_ENABLE_CTRL       3

#define si5351a_CLKOUT_MIN_FREQ          4000
#define si5351a_CLKOUT_MAX_FREQ          si5351a_MULTISYNTH_MAX_FREQ

#define si5351a_OUTPUT_CLK_DIV_1         0
#define si5351a_OUTPUT_CLK_DIV_2         1
#define si5351a_OUTPUT_CLK_DIV_4         2
#define si5351a_OUTPUT_CLK_DIV_8         3
#define si5351a_OUTPUT_CLK_DIV_16        4
#define si5351a_OUTPUT_CLK_DIV_32        5
#define si5351a_OUTPUT_CLK_DIV_64        6
#define si5351a_OUTPUT_CLK_DIV_128       7

#define si5351a_MULTISYNTH_A_MIN         6
#define si5351a_MULTISYNTH_A_MAX         1800

#define si5351a_PLL_RESET                177
#define si5351a_PLL_RESET_B              (1<<7)
#define si5351a_PLL_RESET_A              (1<<5)

#define si5351a_DEVICE_STATUS             0
#define si5351a_INTERRUPT_STATUS          1

#define si5351a_PLL_INPUT_SOURCE          15

#define si5351a_CLK0_PHASE_OFFSET        165
#define si5351a_CLK1_PHASE_OFFSET        166
#define si5351a_CLK2_PHASE_OFFSET        167

#define si5351a_CLK3_0_DISABLE_STATE     24
#define si5351a_CLK7_4_DISABLE_STATE     25

#define si5351a_FANOUT_ENABLE            187
#define si5351a_CLKIN_ENABLE             (1<<7)
#define si5351a_XTAL_ENABLE              (1<<6)
#define si5351a_MULTISYNTH_ENABLE        (1<<4)

#define si5351a_PLLA_SOURCE              (1<<2)
#define si5351a_PLLB_SOURCE              (1<<3)

#define si5351a_CLK_INTEGER_MODE         (1<<6)
#define si5351a_CLK_INVERT               (1<<4)
#define si5351a_CLK_INPUT_MASK           (3<<2)
#define si5351a_CLK_INPUT_XTAL           (0<<2)
#define si5351a_CLK_INPUT_CLKIN          (1<<2)
#define si5351a_CLK_INPUT_MULTISYNTH_0_4 (2<<2)
#define si5351a_CLK_INPUT_MULTISYNTH_N   (3<<2)

#define si5351a_OUTPUT_CLK_DIVBY4        (3<<2)
#define si5351a_OUTPUT_CLK_DIV_SHIFT     4

#define si5351a_CLKIN_DIV_1              (0<<6)
#define si5351a_CLKIN_DIV_2              (1<<6)
#define si5351a_CLKIN_DIV_4              (2<<6)

#define si5351a_REGISTER_3_OUTPUT_ENABLE_CONTROL 3

#define si5351a_REGISTER_16_CLK0_CONTROL 16
#define si5351a_REGISTER_17_CLK1_CONTROL 17
#define si5351a_REGISTER_18_CLK2_CONTROL 18
#define si5351a_REGISTER_19_CLK3_CONTROL 19
#define si5351a_REGISTER_20_CLK4_CONTROL 20
#define si5351a_REGISTER_21_CLK5_CONTROL 21
#define si5351a_REGISTER_22_CLK6_CONTROL 22
#define si5351a_REGISTER_23_CLK7_CONTROL 23
#define si5351a_REGISTER_149_SPREAD_SPECTRUM_PARAMETERS 149
#define si5351a_REGISTER_183_CRYSTAL_INTERNAL_LOAD_CAPACITANCE 183

int si5351a_write_reg(const struct device *dev, uint8_t reg, uint8_t value) {
    const struct si5351a_config *cfg = dev->config;
    uint8_t buf[2] = { reg, value };
    int ret;

    ret = i2c_write_dt(&cfg->i2c, buf, sizeof(buf));
    if (ret) {
        return ret;
    }

    return 0;
}

int si5351a_write_multiple(const struct device *dev, uint8_t start_reg, uint8_t *values, size_t length) {
    const struct si5351a_config *cfg = dev->config;
    uint8_t buf[21];
    int ret;

    __ASSERT(length <= sizeof(buf) - 1, "write_multiple: length too large");

    buf[0] = start_reg;
    memcpy(&buf[1], values, length);

    ret = i2c_write_dt(&cfg->i2c, buf, length + 1);
    if (ret) {
        return ret;
    }

    return 0;
}

int si5351a_read_reg(const struct device *dev, uint8_t reg, uint8_t *value) {
    const struct si5351a_config *cfg = dev->config;
    int ret;

    ret = i2c_write_read_dt(&cfg->i2c, &reg, sizeof(reg), value, sizeof(*value));
    if (ret) {
        return ret;
    }

    return 0;
}

int si5351a_enable_spread_spectrum(const struct device *dev, bool enable) {
    uint8_t reg_val = 0;

    int ret = si5351a_read_reg(dev, si5351a_REGISTER_149_SPREAD_SPECTRUM_PARAMETERS, &reg_val);
    if (ret) {
        return ret;
    }

    if (enable) {
        reg_val |= 0x80;
    } else {
        reg_val &= ~0x80;
    }

    ret = si5351a_write_reg(dev, si5351a_REGISTER_149_SPREAD_SPECTRUM_PARAMETERS, reg_val);
    if (ret) {
        return ret;
    }

    return 0;
}

static int si5351a_init(const struct device *dev) {
    struct si5351a_data *data = dev->data;
    const struct si5351a_config *cfg = dev->config;

    if (!i2c_is_ready_dt(&cfg->i2c)) {
        return -ENODEV;
    }

    uint8_t status_reg = 0;
    int retries = 1000;
    do {
        si5351a_read_reg(dev, si5351a_DEVICE_STATUS, &status_reg);
        if (--retries == 0) {
            return -ETIMEDOUT;
        }
        k_msleep(1);
    } while (status_reg >> 7 == 1);
    
    int ret; 
    ret = si5351a_write_reg(dev, si5351a_REGISTER_3_OUTPUT_ENABLE_CONTROL, 0xFF);
    if (ret < 0) return ret;

    static const uint8_t clk_regs[] = {
        si5351a_REGISTER_16_CLK0_CONTROL,
        si5351a_REGISTER_17_CLK1_CONTROL,
        si5351a_REGISTER_18_CLK2_CONTROL,
        si5351a_REGISTER_19_CLK3_CONTROL,
        si5351a_REGISTER_20_CLK4_CONTROL,
        si5351a_REGISTER_21_CLK5_CONTROL,
        si5351a_REGISTER_22_CLK6_CONTROL,
        si5351a_REGISTER_23_CLK7_CONTROL,
    };

    for (int i = 0; i < ARRAY_SIZE(clk_regs); i++) {
        ret = si5351a_write_reg(dev, clk_regs[i], 0x80);
        if (ret < 0) return ret;
    }

    switch (cfg->crystal_load_capacitance) {
    case 6:
        ret = si5351a_write_reg(dev, si5351a_REGISTER_183_CRYSTAL_INTERNAL_LOAD_CAPACITANCE, si5351a_CRYSTAL_LOAD_6PF);
        break;
    case 8:
        ret = si5351a_write_reg(dev, si5351a_REGISTER_183_CRYSTAL_INTERNAL_LOAD_CAPACITANCE, si5351a_CRYSTAL_LOAD_8PF);
        break;
    case 10:
        ret = si5351a_write_reg(dev, si5351a_REGISTER_183_CRYSTAL_INTERNAL_LOAD_CAPACITANCE, si5351a_CRYSTAL_LOAD_10PF);
        break;
    default:
        return -EINVAL;
    }

    si5351a_enable_spread_spectrum(dev, false);

    data->plla_configured = false;
    data->pllb_configured = false;
    data->plla_freq = 0;
    data->pllb_freq = 0;
    data->initialised = true;

    return 0;
}

int si5351a_update_sys_status(const struct device *dev) {
    struct si5351a_data *data = dev->data;
    uint8_t reg_val = 0;

    int ret = si5351a_read_reg(dev, si5351a_DEVICE_STATUS, &reg_val);
    if (ret) {
        return ret;
    }

    data->dev_status.SYS_INIT = (reg_val >> 7) & 0x01;
    data->dev_status.LOL_B = (reg_val >> 6) & 0x01;
    data->dev_status.LOL_A = (reg_val >> 5) & 0x01;
    data->dev_status.LOS = (reg_val >> 4) & 0x01;
    data->dev_status.REVID = reg_val & 0x03;

    return 0;
}

int si5351a_update_int_status(const struct device *dev) {
    struct si5351a_data *data = dev->data;
    uint8_t reg_val = 0;

    int ret = si5351a_read_reg(dev, si5351a_INTERRUPT_STATUS, &reg_val);
    if (ret) {
        return ret;
    }

    data->dev_int_status.SYS_INIT_STKY = (reg_val >> 7) & 0x01;
    data->dev_int_status.LOL_B_STKY = (reg_val >> 6) & 0x01;
    data->dev_int_status.LOL_A_STKY = (reg_val >> 5) & 0x01;
    data->dev_int_status.LOS_STKY = (reg_val >> 4) & 0x01;

    return 0;
}

int si5351a_update_status(const struct device *dev) {
    int ret = si5351a_update_sys_status(dev);
    if (ret) {
        return ret;
    }
    return si5351a_update_int_status(dev);
}

int si5351a_reset_pll(const struct device *dev, bool reset_a, bool reset_b) {
    uint8_t reg_val = 0;
    if (reset_a) {
        reg_val |= si5351a_PLL_RESET_A;
    }
    if (reset_b) {
        reg_val |= si5351a_PLL_RESET_B;
    }
    return si5351a_write_reg(dev, si5351a_PLL_RESET, reg_val);
}

int si5351a_set_pll(const struct device *dev, char pll, uint32_t a, uint32_t b, uint32_t c) {
    if (pll != 'A' && pll != 'B') {
        return -EINVAL;
    }
    if (a < si5351a_PLL_A_MIN || a > si5351a_PLL_A_MAX) {
        return -EINVAL;
    }
    if (b > si5351a_PLL_B_MAX) {
        return -EINVAL;
    }
    if (c == 0) {
        return -EINVAL;
    }

    uint32_t p1, p2, p3;

    if (b == 0) {
        p1 = 128 * a - 512;
        p2 = b;
        p3 = c;
    } else {
        p1 = (uint32_t)(128 * a + floor(128 * ((double)b / (double)c)) - 512);
        p2 = (128 * b - c * floor(128 * ((double)b / (double)c)));
        p3 = c;
    }

    uint8_t reg_base = (pll == 'A') ? si5351a_PLLA_PARAMETERS : si5351a_PLLB_PARAMETERS;
    uint8_t reg_vals[8] = {
        (p3 & 0x0000FF00) >> 8,
        (p3 & 0x000000FF),
        (p1 & 0x00030000) >> 16,
        (p1 & 0x0000FF00) >> 8,
        (p1 & 0x000000FF),
        ((p3 & 0x000F0000) >> 12) | ((p2 & 0x000F0000) >> 16),
        (p2 & 0x0000FF00) >> 8,
        (p2 & 0x000000FF),
    };

    int ret = si5351a_write_multiple(dev, reg_base, reg_vals, sizeof(reg_vals));
    if (ret) {
        return ret;
    }

    struct si5351a_data *data = dev->data;
    const struct si5351a_config *cfg = dev->config;
    uint32_t freq = (uint32_t)(cfg->xtal_freq * (a + (double)b / c));

    if (pll == 'A') {
        data->plla_configured = true;
        data->plla_freq = freq;
    } else {
        data->pllb_configured = true;
        data->pllb_freq = freq;
    }

    return 0;
}

int si5351a_set_pll_freq(const struct device *dev, char pll, uint32_t freq) {
    const struct si5351a_config *cfg = dev->config;

    if (freq < si5351a_PLL_VCO_MIN || freq > si5351a_PLL_VCO_MAX) {
        return -EINVAL;
    }

    uint32_t a = freq / cfg->xtal_freq;
    uint64_t remainder = (uint64_t)freq % cfg->xtal_freq;
    uint32_t b, c;

    if (remainder == 0) {
        b = 0;
        c = 1;
    } else {
        c = RFRAC_DENOM;
        b = (uint32_t)((remainder * c) / cfg->xtal_freq);
    }

    return si5351a_set_pll(dev, pll, a, b, c);
}

int si5351a_set_ms(const struct device *dev, uint8_t ms, uint32_t a, uint32_t b, uint32_t c, char pll) {
    if (ms > 7) {
        return -EINVAL;
    }
    if (a < si5351a_MULTISYNTH_A_MIN || a > si5351a_MULTISYNTH_A_MAX) {
        return -EINVAL;
    }
    if (b > si5351a_PLL_B_MAX) {
        return -EINVAL;
    }
    if (c == 0) {
        return -EINVAL;
    }
    if (pll != 'A' && pll != 'B') {
        return -EINVAL;
    }

    uint32_t p1, p2, p3;

    if (b == 0) {
        p1 = 128 * a - 512;
        p2 = b;
        p3 = c;
    } else {
        p1 = (uint32_t)(128 * a + floor(128 * ((double)b / (double)c)) - 512);
        p2 = (128 * b - c * floor(128 * ((double)b / (double)c)));
        p3 = c;
    }

    uint8_t reg_base = si5351a_CLK0_PARAMETERS + ms * 8;
    uint8_t reg_vals[8] = {
        (p3 & 0x0000FF00) >> 8,
        (p3 & 0x000000FF),
        (p1 & 0x00030000) >> 16,
        (p1 & 0x0000FF00) >> 8,
        (p1 & 0x000000FF),
        ((p3 & 0x000F0000) >> 12) | ((p2 & 0x000F0000) >> 16),
        (p2 & 0x0000FF00) >> 8,
        (p2 & 0x000000FF),
    };

    int ret = si5351a_write_multiple(dev, reg_base, reg_vals, sizeof(reg_vals));
    if (ret) {
        return ret;
    }

    /* Configure CLK control register: power up output, set source to MSx,
     * select PLL, set 8mA drive strength */
    uint8_t clk_ctrl = si5351a_CLK_INPUT_MULTISYNTH_N | 0x03; /* MSx source, 8mA drive */
    if (pll == 'B') {
        clk_ctrl |= si5351a_CLK_PLL_SELECT;
    }
    if (b == 0) {
        clk_ctrl |= si5351a_CLK_INTEGER_MODE;
    }

    return si5351a_write_reg(dev, si5351a_CLK0_CTRL + ms, clk_ctrl);
}

int si5351a_set_ms_freq(const struct device *dev, uint8_t ms,
                       uint32_t freq_hz, uint32_t freq_millihz, char pll) {
    struct si5351a_data *data = dev->data;

    if (ms > 7) {
        return -EINVAL;
    }
    if (pll != 'A' && pll != 'B') {
        return -EINVAL;
    }
    if (freq_millihz >= 1000) {
        return -EINVAL;
    }

    uint32_t pll_freq = (pll == 'A') ? data->plla_freq : data->pllb_freq;
    if (pll_freq == 0) {
        return -EINVAL;
    }

    uint64_t freq_mhz = (uint64_t)freq_hz * 1000 + freq_millihz;
    if (freq_mhz == 0) {
        return -EINVAL;
    }

    uint64_t pll_freq_mhz = (uint64_t)pll_freq * 1000;
    uint32_t a = (uint32_t)(pll_freq_mhz / freq_mhz);
    uint64_t remainder = pll_freq_mhz % freq_mhz;
    uint32_t c = RFRAC_DENOM;
    uint32_t b = (uint32_t)((remainder * c) / freq_mhz);

    return si5351a_set_ms(dev, ms, a, b, c, pll);
}

int si5351a_enable_output(const struct device *dev, uint8_t output, bool enable) {
    if (output > 7) {
        return -EINVAL;
    }

    uint8_t reg_val = 0;
    int ret = si5351a_read_reg(dev, si5351a_REGISTER_3_OUTPUT_ENABLE_CONTROL, &reg_val);
    if (ret) {
        return ret;
    }

    if (enable) {
        reg_val &= ~(1 << output);
    } else {
        reg_val |= (1 << output);
    }

    return si5351a_write_reg(dev, si5351a_REGISTER_3_OUTPUT_ENABLE_CONTROL, reg_val);
}

#define DT_DRV_COMPAT silabs_si5351a
#define SI5351A_INST(inst)                                          \
    static struct si5351a_data si5351a_data_##inst;                 \
    static const struct si5351a_config si5351a_config_##inst = {    \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                          \
        .xtal_freq = DT_INST_PROP(inst, clock_frequency),           \
        .crystal_load_capacitance = DT_INST_PROP(inst, crystal_load_capacitance) \
    };                                                              \
    DEVICE_DT_INST_DEFINE(inst, si5351a_init, NULL,                 \
                          &si5351a_data_##inst,                     \
                          &si5351a_config_##inst,                   \
                          POST_KERNEL,                              \
                          CONFIG_KERNEL_INIT_PRIORITY_DEVICE,       \
                          NULL);

DT_INST_FOREACH_STATUS_OKAY(SI5351A_INST)