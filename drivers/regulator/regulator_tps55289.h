#ifndef ZEPHYR_DRIVERS_REGULATOR_TPS55289_H_
#define ZEPHYR_DRIVERS_REGULATOR_TPS55289_H_

#include <zephyr/types.h>

/* Register Map */
#define TPS55289_REG_REF_LSB      0x00
#define TPS55289_REG_REF_MSB      0x01
#define TPS55289_REG_IOUT_LIMIT   0x02
#define TPS55289_REG_VOUT_SR      0x03
#define TPS55289_REG_VOUT_FS      0x04
#define TPS55289_REG_CDC          0x05
#define TPS55289_REG_MODE         0x06
#define TPS55289_REG_STATUS       0x07

/* MODE Register Bits (06h) */
#define TPS55289_MODE_OE          BIT(7)
#define TPS55289_MODE_FSWDBL      BIT(6)
#define TPS55289_MODE_HICCUP      BIT(5)
#define TPS55289_MODE_DISCHG      BIT(4)
#define TPS55289_MODE_FPWM        BIT(1)

/* VOUT_FS Register Bits (04h) */
#define TPS55289_FS_FB_SEL        BIT(7)
#define TPS55289_FS_INTFB_MASK    0x03

/* STATUS Register Bits (07h) */
#define TPS55289_STATUS_SCP       BIT(7)
#define TPS55289_STATUS_OCP       BIT(6)
#define TPS55289_STATUS_OVP       BIT(5)
#define TPS55289_STATUS_MODE_MASK 0x03

enum tps55289_mode {
    TPS55289_OP_MODE_BOOST      = 0,
    TPS55289_OP_MODE_BUCK       = 1,
    TPS55289_OP_MODE_BUCK_BOOST = 2,
};

#endif /* ZEPHYR_DRIVERS_REGULATOR_TPS55289_H_ */