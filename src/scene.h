#ifndef N64_SCENE_H
#define N64_SCENE_H

#include <stdbool.h>

typedef enum Scene {
    SPLASH,
    LOGO_SCENE,
    TITLE,
    LEVEL_SELECT,
    GAME,
    PAUSE,
    LEVEL_COMPLETE,
    CUTSCENE_DEMO,
    MAP_TEST,
    DEBUG_MAP,
    MENU_SCENE,
    DEMO_SCENE,
    MULTIPLAYER_SCENE,
    TOTAL_SCENES
} scene_t;

// Request a scene change (deferred until update loop completes)
void change_scene(scene_t scene);

// Immediately change scene - DO NOT call directly, use change_scene() instead
void change_scene_(scene_t scene);

// Update the current scene
void update_current_scene(void);

// Draw the current scene
void draw_current_scene(void);

// Get the current scene
scene_t get_current_scene(void);

// Scene change state (used by main loop)
extern bool should_change_scene;
extern scene_t next_scene;

#endif
