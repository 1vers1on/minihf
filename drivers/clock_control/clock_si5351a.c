#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#define DT_DRV_COMPAT skyworks_si5351a

struct si5351a_config {
    struct i2c_dt_spec i2c;
};

struct si5351a_data {
};

static int si5351a_init(const struct device *dev)
{
    const struct si5351a_config *cfg = dev->config;

    if (!i2c_is_ready_dt(&cfg->i2c)) {
        return -ENODEV;
    }

    return 0;
}

#define SI5351A_INST(inst)                                          \
    static struct si5351a_data si5351a_data_##inst;                 \
    static const struct si5351a_config si5351a_config_##inst = {    \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                         \
    };                                                              \
    DEVICE_DT_INST_DEFINE(inst, si5351a_init, NULL,                 \
                          &si5351a_data_##inst,                     \
                          &si5351a_config_##inst,                   \
                          POST_KERNEL,                              \
                          CONFIG_KERNEL_INIT_PRIORITY_DEVICE,       \
                          NULL);

DT_INST_FOREACH_STATUS_OKAY(SI5351A_INST)
