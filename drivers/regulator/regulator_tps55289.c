#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/logging/log.h>
#include "regulator_tps55289.h"

LOG_MODULE_REGISTER(tps55289, CONFIG_REGULATOR_LOG_LEVEL);

#define DT_DRV_COMPAT ti_tps55289

struct tps55289_config {
    struct i2c_dt_spec i2c;
    bool external_fb;
    uint32_t r_top;
    uint32_t r_bottom;
    int rsense_mohm;
    uint8_t int_fb_ratio;
    uint32_t slew_rate_mv_us;
    bool discharge;
};

struct tps55289_data {
    struct regulator_common_data common;
};

/* --- API Implementation --- */

static int tps55289_enable(const struct device *dev) {
    const struct tps55289_config *cfg = dev->config;
    LOG_INF("Enabling TPS55289 output");
    return i2c_reg_update_byte_dt(&cfg->i2c, TPS55289_REG_MODE, TPS55289_MODE_OE, TPS55289_MODE_OE);
}

static int tps55289_disable(const struct device *dev) {
    const struct tps55289_config *cfg = dev->config;
    LOG_INF("Disabling TPS55289 output");
    return i2c_reg_update_byte_dt(&cfg->i2c, TPS55289_REG_MODE, TPS55289_MODE_OE, 0);
}

static int tps55289_get_status(const struct device *dev, uint8_t *status_reg) {
    const struct tps55289_config *cfg = dev->config;
    return i2c_reg_read_byte_dt(&cfg->i2c, TPS55289_REG_STATUS, status_reg);
}

static int tps55289_set_voltage(const struct device *dev, int32_t min_uv, int32_t max_uv) {
    const struct tps55289_config *cfg = dev->config;
    uint64_t vref_uv;

    if (cfg->external_fb) {
        vref_uv = ((uint64_t)min_uv * cfg->r_bottom) / (cfg->r_top + cfg->r_bottom);
    } else {
        static const uint32_t ratios_x10000[] = {2256, 1128, 752, 564};
        vref_uv = ((uint64_t)min_uv * ratios_x10000[cfg->int_fb_ratio & 0x03]) / 10000;
    }

    /* Clamp Vref: 45mV to 1200mV */
    vref_uv = CLAMP(vref_uv, 45000, 1200000);

    /* Vref = 45mV + (Val * 0.5645mV) -> Val = (Vref - 45) / 0.5645 */
    uint32_t val = (uint32_t)(((vref_uv - 45000ULL) * 10ULL) / 5645ULL);
    uint8_t buf[2] = { val & 0xFF, (val >> 8) & 0x07 };

    return i2c_burst_write_dt(&cfg->i2c, TPS55289_REG_REF_LSB, buf, 2);
}

static int tps55289_set_current_limit(const struct device *dev, int32_t min_ua, int32_t max_ua) {
    const struct tps55289_config *cfg = dev->config;
    if (cfg->rsense_mohm <= 0) return -ENOTSUP;

    uint32_t v_limit_uv = ((uint64_t)min_ua * (uint32_t)cfg->rsense_mohm) / 1000;
    uint8_t val = (uint8_t)(v_limit_uv / 500); /* 1 LSB = 0.5mV */
    if (val > 127) val = 127;

    return i2c_reg_write_byte_dt(&cfg->i2c, TPS55289_REG_IOUT_LIMIT, val | 0x80);
}

/* Initialization */

static int tps55289_init(const struct device *dev) {
    const struct tps55289_config *cfg = dev->config;
    if (!device_is_ready(cfg->i2c.bus)) return -ENODEV;

    /* Set Feedback Source */
    uint8_t fs_val = (cfg->external_fb ? TPS55289_FS_FB_SEL : 0) | (cfg->int_fb_ratio & 0x03);
    i2c_reg_write_byte_dt(&cfg->i2c, TPS55289_REG_VOUT_FS, fs_val);

    /* Set Slew Rate (simplifying bits: 1250=0, 2500=1, 5000=2, 10000=3) */
    uint8_t sr_bits = (cfg->slew_rate_mv_us >= 10000) ? 3 : 
                      (cfg->slew_rate_mv_us >= 5000)  ? 2 : 
                      (cfg->slew_rate_mv_us >= 2500)  ? 1 : 0;
    i2c_reg_write_byte_dt(&cfg->i2c, TPS55289_REG_VOUT_SR, sr_bits);

    /* Initial Mode Setup */
    uint8_t mode_val = (cfg->discharge ? TPS55289_MODE_DISCHG : 0) | TPS55289_MODE_HICCUP;
    i2c_reg_write_byte_dt(&cfg->i2c, TPS55289_REG_MODE, mode_val);

    return 0;
}

static const struct regulator_driver_api tps55289_api = {
    .enable = tps55289_enable,
    .disable = tps55289_disable,
    .set_voltage = tps55289_set_voltage,
    .set_current_limit = tps55289_set_current_limit,
};

#define TPS55289_DEVICE(inst)                                                                      \
    static struct tps55289_data tps55289_data_##inst;                                              \
    static const struct tps55289_config tps55289_config_##inst = {                                 \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                                                         \
        .external_fb = DT_INST_PROP(inst, ti_external_feedback),                                   \
        .r_top = DT_INST_PROP_BY_IDX(inst, ti_feedback_resistors_ohms, 0),                         \
        .r_bottom = DT_INST_PROP_BY_IDX(inst, ti_feedback_resistors_ohms, 1),                      \
        .rsense_mohm = DT_INST_PROP(inst, ti_rsense_mohm),                                         \
        .int_fb_ratio = DT_INST_PROP(inst, ti_internal_fb_ratio),                                  \
        .slew_rate_mv_us = DT_INST_PROP(inst, ti_slew_rate_mv_us),                                 \
        .discharge = DT_INST_PROP(inst, ti_discharge_enable),                                      \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(inst, tps55289_init, NULL, &tps55289_data_##inst,                        \
                          &tps55289_config_##inst, POST_KERNEL,                                    \
                          CONFIG_REGULATOR_TPS55289_INIT_PRIORITY, &tps55289_api);

DT_INST_FOREACH_STATUS_OKAY(TPS55289_DEVICE)
