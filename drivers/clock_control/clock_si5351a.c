#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include "clock_si5351a.h"
#include <zephyr/drivers/clock_control.h>

#define DT_DRV_COMPAT silabs_si5351a

#define SI5351_PLL_VCO_MIN 600000000ULL
#define SI5351_PLL_VCO_MAX 900000000ULL

#define SI5351_MULTISYNTH_MIN_FREQ      500000ULL
#define SI5351_MULTISYNTH_DIVBY4_FREQ   150000000ULL
#define SI5351_MULTISYNTH_MAX_FREQ      200000000ULL
#define SI5351_MULTISYNTH_SHARE_MAX     100000000ULL

#define SI5351_PLL_FIXED                80000000000ULL

#define SI5351_CRYSTAL_LOAD 183
#define SI5351_CRYSTAL_LOAD_MASK        (3<<6)
#define SI5351_CRYSTAL_LOAD_0PF         (0<<6)
#define SI5351_CRYSTAL_LOAD_6PF         (1<<6)
#define SI5351_CRYSTAL_LOAD_8PF         (2<<6)
#define SI5351_CRYSTAL_LOAD_10PF        (3<<6)

#define SI5351_PLL_A_MIN                15
#define SI5351_PLL_A_MAX                90
#define SI5351_PLL_B_MAX                1048574
#define RFRAC_DENOM 1000000ULL

#define SI5351_PLLA_PARAMETERS          26
#define SI5351_PLLB_PARAMETERS          34
#define SI5351_CLK0_PARAMETERS          42
#define SI5351_CLK1_PARAMETERS          50
#define SI5351_CLK2_PARAMETERS          58

#define SI5351_CLK0_CTRL                16
#define SI5351_CLK1_CTRL                17
#define SI5351_CLK2_CTRL                18
#define SI5351_CLK_PLL_SELECT           (1<<5)
#define SI5351_OUTPUT_ENABLE_CTRL       3

#define SI5351_CLKOUT_MIN_FREQ          4000
#define SI5351_CLKOUT_MAX_FREQ          SI5351_MULTISYNTH_MAX_FREQ

#define SI5351_OUTPUT_CLK_DIV_1         0
#define SI5351_OUTPUT_CLK_DIV_2         1
#define SI5351_OUTPUT_CLK_DIV_4         2
#define SI5351_OUTPUT_CLK_DIV_8         3
#define SI5351_OUTPUT_CLK_DIV_16        4
#define SI5351_OUTPUT_CLK_DIV_32        5
#define SI5351_OUTPUT_CLK_DIV_64        6
#define SI5351_OUTPUT_CLK_DIV_128       7

#define SI5351_MULTISYNTH_A_MIN         6
#define SI5351_MULTISYNTH_A_MAX         1800

#define SI5351_PLL_RESET                177
#define SI5351_PLL_RESET_B              (1<<7)
#define SI5351_PLL_RESET_A              (1<<5)

#define SI5351_CLK0_PHASE_OFFSET        165
#define SI5351_CLK1_PHASE_OFFSET        166
#define SI5351_CLK2_PHASE_OFFSET        167

# define do_div(n,base) ({                                      \
        uint64_t __base = (base);                               \
        uint64_t __rem;                                         \
        __rem = ((uint64_t)(n)) % __base;                       \
        (n) = ((uint64_t)(n)) / __base;                         \
        __rem;                                                  \
 })

static int si5351a_write_reg(const struct device *dev, uint8_t reg, uint8_t value)
{
    const struct si5351a_config *cfg = dev->config;
    uint8_t buf[2] = { reg, value };
    int ret;

    ret = i2c_write_dt(&cfg->i2c, buf, sizeof(buf));
    if (ret) {
        return ret;
    }

    return 0;
}

static int si5351a_write_multiple(const struct device *dev, uint8_t start_reg, uint8_t *values, size_t length)
{
    const struct si5351a_config *cfg = dev->config;
    uint8_t *buf = k_malloc(length + 1);
    int ret;

    buf[0] = start_reg;
    memcpy(&buf[1], values, length);

    ret = i2c_write_dt(&cfg->i2c, buf, length + 1);
    k_free(buf);
    if (ret) {
        return ret;
    }

    return 0;
}

static int si5351a_read_reg(const struct device *dev, uint8_t reg, uint8_t *value)
{
    const struct si5351a_config *cfg = dev->config;
    int ret;

    ret = i2c_write_read_dt(&cfg->i2c, &reg, sizeof(reg), value, sizeof(*value));
    if (ret) {
        return ret;
    }

    return 0;
}

static void reset_pll(const struct device *dev, uint8_t pll) {
    uint8_t reg_val;

    if (pll == 0) {
        reg_val = SI5351_PLL_RESET_A;
    } else {
        reg_val = SI5351_PLL_RESET_B;
    }

    si5351a_write_reg(dev, SI5351_PLL_RESET, reg_val);
}

uint64_t si5351a_calc_pll(uint8_t target_pll, uint64_t freq, uint32_t xtal_freq, struct si5351a_reg_set *reg_set) {
    uint64_t ref_freq = xtal_freq * SI5351A_FREQ_MULT;

    uint32_t a, b, c, p1, p2, p3;
	uint64_t lltmp; //, denom;

    if (freq < SI5351_PLL_VCO_MIN * SI5351A_FREQ_MULT)
	{
		freq = SI5351_PLL_VCO_MIN * SI5351A_FREQ_MULT;
	}
	if (freq > SI5351_PLL_VCO_MAX * SI5351A_FREQ_MULT)
	{
		freq = SI5351_PLL_VCO_MAX * SI5351A_FREQ_MULT;
	}

    a = freq / ref_freq;

    if (a < SI5351_PLL_A_MIN)
	{
		freq = ref_freq * SI5351_PLL_A_MIN;
	}
	if (a > SI5351_PLL_A_MAX)
	{
		freq = ref_freq * SI5351_PLL_A_MAX;
	}

    b = (((uint64_t)(freq % ref_freq)) * RFRAC_DENOM) / ref_freq;
    c = b ? RFRAC_DENOM : 1;

    p1 = 128 * a + ((128 * b) / c) - 512;
    p2 = 128 * b - c * ((128 * b) / c);
    p3 = c;

    lltmp = ref_freq;
	lltmp *= b;

    do_div(lltmp, c);
	freq = lltmp;
	freq += ref_freq * a;

	reg_set->p1 = p1;
	reg_set->p2 = p2;
	reg_set->p3 = p3;

    return freq;
}

void si5351a_set_pll(const struct device *dev, uint8_t target_pll, uint64_t pll_freq) {
    struct si5351a_data *data = dev->data;
    const struct si5351a_config *cfg = dev->config;
    struct si5351a_reg_set reg_set;
    uint8_t base_addr;
    int ret;

    pll_freq = si5351a_calc_pll(target_pll, pll_freq, cfg->xtal_freq, &reg_set);

    uint8_t *params = k_malloc(20);
    uint8_t i = 0;
    uint8_t temp;

    temp = ((reg_set.p3 >> 8) & 0xFF);
    params[i++] = temp;

    temp = (uint8_t)(reg_set.p3  & 0xFF);
    params[i++] = temp;

    temp = (uint8_t)((reg_set.p1 >> 16) & 0x03);
    params[i++] = temp;

    temp = (uint8_t)((reg_set.p1 >> 8) & 0xFF);
    params[i++] = temp;

    temp = (uint8_t)(reg_set.p1  & 0xFF);
    params[i++] = temp;

    temp = (uint8_t)((reg_set.p3 >> 12) & 0xF0);
    temp += (uint8_t)((reg_set.p2 >> 16) & 0x0F);
    params[i++] = temp;

    temp = (uint8_t)((reg_set.p2 >> 8) & 0xFF);
    params[i++] = temp;

    temp = (uint8_t)(reg_set.p2  & 0xFF);
    params[i++] = temp;

    if (target_pll == 0) {
        base_addr = SI5351_PLLA_PARAMETERS;
        data->plla_freq = pll_freq;
    } else {
        base_addr = SI5351_PLLB_PARAMETERS;
        data->pllb_freq = pll_freq;
    }

    ret = si5351a_write_multiple(dev, base_addr, params, i);
    k_free(params);
}

static void set_int(const struct device *dev, uint8_t synth, uint8_t enable) {
    uint8_t reg_val;

    si5351a_read_reg(dev, (SI5351_CLK0_CTRL + synth), &reg_val);
    if (enable) {
        reg_val |= (1 << 6);
    } else {
        reg_val &= ~(1 << 6);
    }

    si5351a_write_reg(dev, (SI5351_CLK0_CTRL + synth), reg_val);
}

static void ms_div(const struct device *dev, uint8_t synth, uint8_t rdiv, uint8_t div_by_4) {
    uint8_t reg_val = 0;
    uint8_t reg_addr = 0;
    
    switch (synth) {
        case 0:
            reg_addr = SI5351_CLK0_PARAMETERS + 2;
            break;
        case 1:
            reg_addr = SI5351_CLK1_PARAMETERS + 2;
            break;
        case 2:
            reg_addr = SI5351_CLK2_PARAMETERS + 2;
            break;
    }

    si5351a_read_reg(dev, reg_addr, &reg_val);

    reg_val &= ~(0x7c);

    if(div_by_4 == 0) {
		reg_val &= ~(3 << 2);
	} else {
		reg_val |= (3 << 2);
	}

    reg_val |= (rdiv << 4);

    si5351a_write_reg(dev, reg_addr, reg_val);
}

void si5351a_set_multisynth(const struct device *dev, uint8_t synth, struct si5351a_reg_set reg_set, uint8_t int_mode, uint8_t rdiv, uint8_t div_by_4) {
    uint8_t *params = k_malloc(20);
	uint8_t i = 0;
 	uint8_t temp;
 	uint8_t reg_val;

    temp = (uint8_t)((reg_set.p3 >> 8) & 0xFF);
	params[i++] = temp;

	temp = (uint8_t)(reg_set.p3  & 0xFF);
	params[i++] = temp;

    si5351a_read_reg(dev, (SI5351_CLK0_PARAMETERS + 2) + (synth * 8), &reg_val);
    reg_val &= ~(0x03);
    temp = reg_val | ((uint8_t)((reg_set.p1 >> 16) & 0x03));
    params[i++] = temp;

    temp = (uint8_t)((reg_set.p1 >> 8) & 0xFF);
    params[i++] = temp;

    temp = (uint8_t)(reg_set.p1  & 0xFF);
    params[i++] = temp;

    temp = (uint8_t)((reg_set.p3 >> 12) & 0xF0);
    temp += (uint8_t)((reg_set.p2 >> 16) & 0x0F);
    params[i++] = temp;

    temp = (uint8_t)((reg_set.p2 >> 8) & 0xFF);
    params[i++] = temp;

    temp = (uint8_t)(reg_set.p2  & 0xFF);
    params[i++] = temp;
    
    switch (synth) {
        case 0:
            si5351a_write_multiple(dev, SI5351_CLK0_PARAMETERS, params, i);
            set_int(dev, 0, int_mode);
            ms_div(dev, 0, rdiv, div_by_4);
            break;
        case 1:
            si5351a_write_multiple(dev, SI5351_CLK1_PARAMETERS, params, i);
            set_int(dev, 1, int_mode);
            ms_div(dev, 1, rdiv, div_by_4);
            break;
        case 2:
            si5351a_write_multiple(dev, SI5351_CLK2_PARAMETERS, params, i);
            set_int(dev, 2, int_mode);
            ms_div(dev, 2, rdiv, div_by_4);
            break;
    }

    k_free(params);
}

uint8_t select_r_div(uint64_t* freq) {
    uint8_t r_div = SI5351_OUTPUT_CLK_DIV_1;

    if ((*freq >= SI5351_CLKOUT_MIN_FREQ * SI5351A_FREQ_MULT) && (*freq < SI5351_CLKOUT_MIN_FREQ * SI5351A_FREQ_MULT * 2)) {
        r_div = SI5351_OUTPUT_CLK_DIV_128;
        *freq *= 128ULL;
    } else if ((*freq >= SI5351_CLKOUT_MIN_FREQ * SI5351A_FREQ_MULT * 2) && (*freq < SI5351_CLKOUT_MIN_FREQ * SI5351A_FREQ_MULT * 4)) {
        r_div = SI5351_OUTPUT_CLK_DIV_64;
        *freq *= 64ULL;
    } else if ((*freq >= SI5351_CLKOUT_MIN_FREQ * SI5351A_FREQ_MULT * 4) && (*freq < SI5351_CLKOUT_MIN_FREQ * SI5351A_FREQ_MULT * 8)) {
        r_div = SI5351_OUTPUT_CLK_DIV_32;
        *freq *= 32ULL;
    } else if ((*freq >= SI5351_CLKOUT_MIN_FREQ * SI5351A_FREQ_MULT * 8) && (*freq < SI5351_CLKOUT_MIN_FREQ * SI5351A_FREQ_MULT * 16)) {
        r_div = SI5351_OUTPUT_CLK_DIV_16;
        *freq *= 16ULL;
    } else if ((*freq >= SI5351_CLKOUT_MIN_FREQ * SI5351A_FREQ_MULT * 16) && (*freq < SI5351_CLKOUT_MIN_FREQ * SI5351A_FREQ_MULT * 32)) {
        r_div = SI5351_OUTPUT_CLK_DIV_8;
        *freq *= 8ULL;
    } else if ((*freq >= SI5351_CLKOUT_MIN_FREQ * SI5351A_FREQ_MULT * 32) && (*freq < SI5351_CLKOUT_MIN_FREQ * SI5351A_FREQ_MULT * 64)) {
        r_div = SI5351_OUTPUT_CLK_DIV_4;
        *freq *= 4ULL;
    } else if ((*freq >= SI5351_CLKOUT_MIN_FREQ * SI5351A_FREQ_MULT * 64) && (*freq < SI5351_CLKOUT_MIN_FREQ * SI5351A_FREQ_MULT * 128)) {
        r_div = SI5351_OUTPUT_CLK_DIV_2;
        *freq *= 2ULL;
    }

    return r_div;
}

uint64_t multisynth_calc(uint64_t freq, uint64_t pll_freq, struct si5351a_reg_set *reg_set) {
    uint64_t lltmp;
	uint32_t a, b, c, p1, p2, p3;
	uint8_t divby4 = 0;
	uint8_t ret_val = 0;

    if (freq > SI5351_MULTISYNTH_MAX_FREQ * SI5351A_FREQ_MULT) {
		freq = SI5351_MULTISYNTH_MAX_FREQ * SI5351A_FREQ_MULT;
	}
	if (freq < SI5351_MULTISYNTH_MIN_FREQ * SI5351A_FREQ_MULT) {
		freq = SI5351_MULTISYNTH_MIN_FREQ * SI5351A_FREQ_MULT;
	}

    if (freq >= SI5351_MULTISYNTH_DIVBY4_FREQ * SI5351A_FREQ_MULT) {
		divby4 = 1;
	}

    if(pll_freq == 0) {
        if(divby4 == 0) {
			lltmp = SI5351_PLL_VCO_MAX * SI5351A_FREQ_MULT;
			do_div(lltmp, freq);
			if(lltmp == 5) {
				lltmp = 4;
			} else if(lltmp == 7) {
				lltmp = 6;
			}
			a = (uint32_t)lltmp;

            b = 0;
		    c = 1;
		    pll_freq = a * freq;
		} else {
			a = 4;
		}
    } else {
        ret_val = 1;
        a = pll_freq / freq;

        if (a < SI5351_MULTISYNTH_A_MIN) {
			freq = pll_freq / SI5351_MULTISYNTH_A_MIN;
		}
		if (a > SI5351_MULTISYNTH_A_MAX) {
			freq = pll_freq / SI5351_MULTISYNTH_A_MAX;
		}

        b = (pll_freq % freq * RFRAC_DENOM) / freq;
		c = b ? RFRAC_DENOM : 1;
    }

    if (divby4 == 1) {
		p3 = 1;
		p2 = 0;
		p1 = 0;
	} else {
	    p1 = 128 * a + ((128 * b) / c) - 512;
	    p2 = 128 * b - c * ((128 * b) / c);
	    p3 = c;
	}

    reg_set->p1 = p1;
	reg_set->p2 = p2;
	reg_set->p3 = p3;

    if(ret_val == 0) {
		return pll_freq;
	} else {
		return freq;
	}
}

void si5351a_set_ms_source(const struct device *dev, uint8_t clk, uint8_t pll) {
    struct si5351a_data *data = dev->data;
    uint8_t reg_val;

    si5351a_read_reg(dev, SI5351_CLK0_CTRL + clk, &reg_val);

    if (pll == 0) {
        reg_val &= ~(SI5351_CLK_PLL_SELECT);
    } else {
        reg_val |= SI5351_CLK_PLL_SELECT;
    }

    si5351a_write_reg(dev, SI5351_CLK0_CTRL + clk, reg_val);

    data->pll_assignments[clk] = pll;
}

void si5351a_output_enable(const struct device *dev, uint8_t clk, uint8_t enable) {
    uint8_t reg_val;

    si5351a_read_reg(dev, SI5351_OUTPUT_ENABLE_CTRL, &reg_val);

    if (enable == 1) {
        reg_val &= ~(1 << clk);
    } else {
        reg_val |= (1 << clk);
    }

    si5351a_write_reg(dev, SI5351_OUTPUT_ENABLE_CTRL, reg_val);
}

void si5351a_set_phase(const struct device *dev, uint8_t clk, uint8_t phase) {
    phase = phase & 0b01111111;

    si5351a_write_reg(dev, SI5351_CLK0_PHASE_OFFSET + clk, phase);
}

static int si5351a_init(const struct device *dev)
{
    struct si5351a_data *data = dev->data;
    const struct si5351a_config *cfg = dev->config;

    if (!i2c_is_ready_dt(&cfg->i2c)) {
        return -ENODEV;
    }

    uint8_t mask;
    switch (cfg->crystal_load_capacitance) {
    case 0:
        mask = SI5351_CRYSTAL_LOAD_0PF;
        break;
    case 6:
        mask = SI5351_CRYSTAL_LOAD_6PF;
        break;
    case 8:
        mask = SI5351_CRYSTAL_LOAD_8PF;
        break;
    case 10:
        mask = SI5351_CRYSTAL_LOAD_10PF;
        break;
    default:
        return -EINVAL;
    }

    mask = (mask & SI5351_CRYSTAL_LOAD_MASK) | 0b00010010;
    si5351a_write_reg(dev, SI5351_CRYSTAL_LOAD, mask);

    si5351a_write_reg(dev, 16, 0x80);
    si5351a_write_reg(dev, 17, 0x80);
    si5351a_write_reg(dev, 18, 0x80);
    si5351a_write_reg(dev, 19, 0x80);
    si5351a_write_reg(dev, 20, 0x80);
    si5351a_write_reg(dev, 21, 0x80);
    si5351a_write_reg(dev, 22, 0x80);
    si5351a_write_reg(dev, 23, 0x80);

    si5351a_write_reg(dev, 16, 0x0c);
    si5351a_write_reg(dev, 17, 0x0c);
    si5351a_write_reg(dev, 18, 0x0c);
    si5351a_write_reg(dev, 19, 0x0c);
    si5351a_write_reg(dev, 20, 0x0c);
    si5351a_write_reg(dev, 21, 0x0c);
    si5351a_write_reg(dev, 22, 0x0c);
    si5351a_write_reg(dev, 23, 0x0c);

    si5351a_set_pll(dev, 0, SI5351_PLL_FIXED);
    si5351a_set_pll(dev, 1, SI5351_PLL_FIXED);

    data->pll_assignments[0] = 0;
    data->pll_assignments[1] = 0;
    data->pll_assignments[2] = 0;

    si5351a_set_ms_source(dev, 0, 0);
    si5351a_set_ms_source(dev, 1, 0);
    si5351a_set_ms_source(dev, 2, 0);

    reset_pll(dev, 0);
    reset_pll(dev, 1);

    return 0;
}

uint8_t si5351a_set_freq(const struct device *dev, uint8_t output, uint64_t freq) {
    struct si5351a_data *data = dev->data;
    const struct si5351a_config *cfg = dev->config;
    struct si5351a_reg_set reg_set;
	uint64_t pll_freq;
	uint8_t int_mode = 0;
	uint8_t div_by_4 = 0;
	uint8_t r_div = 0;

    if(freq > 0 && freq < SI5351_CLKOUT_MIN_FREQ * SI5351A_FREQ_MULT) {
        freq = SI5351_CLKOUT_MIN_FREQ * SI5351A_FREQ_MULT;
    }

    if(freq > SI5351_MULTISYNTH_MAX_FREQ * SI5351A_FREQ_MULT) {
        freq = SI5351_MULTISYNTH_MAX_FREQ * SI5351A_FREQ_MULT;
    }

    if(freq > (SI5351_MULTISYNTH_SHARE_MAX * SI5351A_FREQ_MULT)) {
        // TODO: idk
    } else {
        data->output_freq[output] = freq;
        r_div = select_r_div(&freq);

        if (data->pll_assignments[output] == 0) {
            multisynth_calc(freq, data->plla_freq, &reg_set);

        } else {
            multisynth_calc(freq, data->pllb_freq, &reg_set);
        }

        si5351a_set_multisynth(dev, output, reg_set, int_mode, r_div, div_by_4);
    }

    return 0;
}


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

