#include "hardware/oled.h"
#include "config.h"
#include <zephyr/drivers/display.h>
#include <zephyr/display/cfb.h>
#include <zephyr/kernel.h>
#include <stdio.h>

static const struct device *oled_dev = DEVICE_DT_GET(DT_NODELABEL(ssd1306));
static bool oled_initialized = false;
char _oled_print_buf[128];

int init_oled(void) {
    debug_printf("[OLED] Starting OLED init");
    for (int tries = 0; tries < OLED_TRY_COUNT; tries++) {
        if (device_is_ready(oled_dev)) {
            break;
        }
        debug_printf("[OLED] Waiting for device (attempt %d/%d)", tries + 1, OLED_TRY_COUNT);
        k_sleep(K_SECONDS(1));
        if (tries == OLED_TRY_COUNT - 1) {
            debug_printf("[OLED] Device not ready after %d attempts", OLED_TRY_COUNT);
            return -ENODEV;
        }
    }
    debug_printf("[OLED] Initializing framebuffer");
    int ret = cfb_framebuffer_init(oled_dev);
    if (ret != 0) {
        debug_printf("[OLED] Failed to initialize framebuffer: %d", ret);
        return ret;
    }
    cfb_framebuffer_clear(oled_dev, true);
    debug_printf("[OLED] Setting font");
    ret = cfb_framebuffer_set_font(oled_dev, FONT);
    if (ret != 0) {
        debug_printf("[OLED] Failed to set font: %d", ret);
        return ret;
    }
    debug_printf("[OLED] Turning on display");
    ret = display_blanking_off(oled_dev);
    if (ret != 0) {
        debug_printf("[OLED] Failed to turn on display: %d", ret);
        return ret;
    }
    display_set_contrast(oled_dev, 255);
    oled_initialized = true;
    return 0;
}

int oled_print(const char *str, uint16_t x, uint16_t y) {
    if (!oled_initialized) return -ENODEV;
    return cfb_print(oled_dev, str, x, y);
}

int oled_clear(void) {
    if (!oled_initialized) return -ENODEV;
    return cfb_framebuffer_clear(oled_dev, true);
}

int oled_flush(void) {
    if (!oled_initialized) return -ENODEV;
    return cfb_framebuffer_finalize(oled_dev);
}

int oled_set_pixel(uint16_t x, uint16_t y, bool on) {
    if (!oled_initialized) return -ENODEV;
    struct cfb_position pos = { .x = x, .y = y };
    int ret = cfb_draw_point(oled_dev, &pos);
    if (ret != 0) {
        return ret;
    }
    if (!on) {
        return cfb_invert_area(oled_dev, x, y, 1, 1);
    }
    return 0;
}

int oled_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    if (!oled_initialized) return -ENODEV;
    struct cfb_position start = { .x = x0, .y = y0 };
    struct cfb_position end   = { .x = x1, .y = y1 };
    return cfb_draw_line(oled_dev, &start, &end);
}
