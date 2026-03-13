#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <zephyr/kernel.h>

typedef enum {
    DISPLAY_SCENE_BOOT,
    DISPLAY_SCENE_MAIN,
    DISPLAY_SCENE_SPECTRUM,
    
} display_scene_t;

void display_manager_init(void);
void display_set_scene(display_scene_t new_scene);
display_scene_t display_get_scene(void);

#endif // DISPLAY_MANAGER_H