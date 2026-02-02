#include <libdragon.h>
#include "scene.h"

#include "scenes/splash.h"
#include "scenes/logo_scene.h"

// Set FPU to flush denormals to zero and disable FPU exceptions
// Called before scene transitions to ensure clean FPU state
static void fpu_flush_denormals(void) {
    uint32_t fcr31;
    __asm__ volatile("cfc1 %0, $31" : "=r"(fcr31));
    fcr31 |= (1 << 24);  // Set FS bit (Flush denormalized results to zero)
    fcr31 &= ~(0x1F << 7);  // Clear all exception enable bits (bits 7-11)
    fcr31 &= ~(0x3F << 2);  // Clear all cause bits (bits 2-6)
    fcr31 &= ~(1 << 17);    // Clear cause bit for unimplemented operation
    fcr31 &= ~(0x1F << 12); // Clear sticky FLAG bits (bits 12-16)
    __asm__ volatile("ctc1 %0, $31" : : "r"(fcr31));
}
#include "scenes/title.h"
#include "scenes/level_select.h"
#include "scenes/game.h"
#include "scenes/pause.h"
#include "scenes/level_complete.h"
#include "scenes/cutscene_demo.h"
#include "scenes/map_test.h"
#include "scenes/debug_map.h"
#include "scenes/menu_scene.h"
#include "scenes/demo_scene.h"
#include "scenes/multiplayer.h"

static scene_t current_scene_ = SPLASH;

bool should_change_scene = false;
scene_t next_scene = TITLE;

void change_scene(scene_t new_scene) {
    should_change_scene = true;
    next_scene = new_scene;
}

void change_scene_(scene_t scene) {
    static const void (*init_scene_functions[])(void) = {
        [SPLASH] = init_splash_scene,
        [LOGO_SCENE] = init_logo_scene,
        [TITLE] = init_title_scene,
        [LEVEL_SELECT] = init_level_select_scene,
        [GAME] = init_game_scene,
        [PAUSE] = init_pause_scene,
        [LEVEL_COMPLETE] = init_level_complete_scene,
        [CUTSCENE_DEMO] = init_cutscene_demo_scene,
        [MAP_TEST] = init_map_test_scene,
        [DEBUG_MAP] = init_debug_map_scene,
        [MENU_SCENE] = init_menu_scene,
        [DEMO_SCENE] = init_demo_scene,
        [MULTIPLAYER_SCENE] = init_multiplayer_scene,
    };

    static const void (*deinit_scene_functions[])(void) = {
        [SPLASH] = deinit_splash_scene,
        [LOGO_SCENE] = deinit_logo_scene,
        [TITLE] = deinit_title_scene,
        [LEVEL_SELECT] = cleanup_level_select_scene,
        [GAME] = deinit_game_scene,
        [PAUSE] = deinit_pause_scene,
        [LEVEL_COMPLETE] = deinit_level_complete_scene,
        [CUTSCENE_DEMO] = deinit_cutscene_demo_scene,
        [MAP_TEST] = deinit_map_test_scene,
        [DEBUG_MAP] = deinit_debug_map_scene,
        [MENU_SCENE] = deinit_menu_scene,
        [DEMO_SCENE] = deinit_demo_scene,
        [MULTIPLAYER_SCENE] = deinit_multiplayer_scene,
    };

    // Ensure clean FPU state before scene transitions
    fpu_flush_denormals();

    deinit_scene_functions[current_scene_]();
    init_scene_functions[scene]();

    current_scene_ = scene;
    should_change_scene = false;
}

void update_current_scene(void) {
    // Ensure clean FPU state before update
    fpu_flush_denormals();

    static const void (*update_scene_functions[])(void) = {
        [SPLASH] = update_splash_scene,
        [LOGO_SCENE] = update_logo_scene,
        [TITLE] = update_title_scene,
        [LEVEL_SELECT] = update_level_select_scene,
        [GAME] = update_game_scene,
        [PAUSE] = update_pause_scene,
        [LEVEL_COMPLETE] = update_level_complete_scene,
        [CUTSCENE_DEMO] = update_cutscene_demo_scene,
        [MAP_TEST] = update_map_test_scene,
        [DEBUG_MAP] = update_debug_map_scene,
        [MENU_SCENE] = update_menu_scene,
        [DEMO_SCENE] = update_demo_scene,
        [MULTIPLAYER_SCENE] = update_multiplayer_scene,
    };
    update_scene_functions[current_scene_]();
}

void draw_current_scene(void) {
    // Ensure clean FPU state before draw
    fpu_flush_denormals();

    static const void (*draw_scene_functions[])(void) = {
        [SPLASH] = draw_splash_scene,
        [LOGO_SCENE] = draw_logo_scene,
        [TITLE] = draw_title_scene,
        [LEVEL_SELECT] = draw_level_select_scene,
        [GAME] = draw_game_scene,
        [PAUSE] = draw_pause_scene,
        [LEVEL_COMPLETE] = draw_level_complete_scene,
        [CUTSCENE_DEMO] = draw_cutscene_demo_scene,
        [MAP_TEST] = draw_map_test_scene,
        [DEBUG_MAP] = draw_debug_map_scene,
        [MENU_SCENE] = draw_menu_scene,
        [DEMO_SCENE] = draw_demo_scene,
        [MULTIPLAYER_SCENE] = draw_multiplayer_scene,
    };
    draw_scene_functions[current_scene_]();
}

scene_t get_current_scene(void) {
    return current_scene_;
}
