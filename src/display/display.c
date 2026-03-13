#include "display/display.h"
#include "hardware/oled.h"
#include "config.h"
#include "radio/radio.h"
#include "radio/reciever.h"

#define DISPLAY_THREAD_STACK_SIZE 1024
#define DISPLAY_THREAD_PRIORITY   7
#define DISPLAY_REFRESH_MS        100

static display_scene_t current_scene = DISPLAY_SCENE_BOOT;
static K_MUTEX_DEFINE(scene_mutex);

static void render_scene_boot(void) {
    oled_print("Booting MiniHF...", 0, 10);
}

static void render_scene_main(void) {
    oled_print("Main Menu", 0, 0);
}

static void render_scene_spectrum(void) {
    oled_print("Spectrum", 0, 0);
    if (tx_active) {
        oled_print("TX Active", 0, 20);
        return;
    }

    const int width = 128;
    const int height = 64;
    const int bar_height = 40;
    const int y_offset = 24;
    for (int i = 0; i < width; ++i) {
        uint8_t value = fft_buffer[i];
        int h = (value * bar_height) / 255;
        if (h > bar_height) h = bar_height;
        for (int y = 0; y < h; ++y) {
            oled_set_pixel(i, y_offset + (bar_height - y), true);
        }
    }
}

static void display_thread_fn(void *arg1, void *arg2, void *arg3) {
    display_scene_t active_scene;

    if (init_oled() != 0) {
        debug_printf("[DISPLAY] OLED init failed in display thread, exiting thread");
        return; 
    }

    while (1) {
        k_mutex_lock(&scene_mutex, K_FOREVER);
        active_scene = current_scene;
        k_mutex_unlock(&scene_mutex);

        oled_clear();

        switch (active_scene) {
            case DISPLAY_SCENE_BOOT:
                render_scene_boot();
                break;
            case DISPLAY_SCENE_MAIN:
                render_scene_main();
                break;
            case DISPLAY_SCENE_SPECTRUM:
                render_scene_spectrum();
                break;
            default:
                oled_print("Scene Error!", 0, 0);
                break;
        }

        oled_flush();

        k_msleep(DISPLAY_REFRESH_MS);
    }
}

K_THREAD_DEFINE(display_thread_id, DISPLAY_THREAD_STACK_SIZE,
                display_thread_fn, NULL, NULL, NULL,
                DISPLAY_THREAD_PRIORITY, 0, 0);

void display_manager_init(void) {
    debug_printf("[DISPLAY] Display manager initialized.");
}

void display_set_scene(display_scene_t new_scene) {
    k_mutex_lock(&scene_mutex, K_FOREVER);
    current_scene = new_scene;
    k_mutex_unlock(&scene_mutex);
}

display_scene_t display_get_scene(void) {
    display_scene_t scene;
    k_mutex_lock(&scene_mutex, K_FOREVER);
    scene = current_scene;
    k_mutex_unlock(&scene_mutex);
    return scene;
}
