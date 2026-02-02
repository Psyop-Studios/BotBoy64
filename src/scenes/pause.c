#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/tpx.h>
#include "pause.h"
#include "game.h"
#include "../scene.h"
#include "../save.h"

// Menu options
typedef enum {
    PAUSE_RESUME,
    PAUSE_RESTART,
    PAUSE_QUIT,
    PAUSE_OPTION_COUNT
} PauseOption;

static int selectedOption = 0;
static float cursorBob = 0.0f;  // For cursor animation

void init_pause_scene(void) {
    selectedOption = 0;
    cursorBob = 0.0f;
}

void deinit_pause_scene(void) {
    // Cleanup pause screen resources here
}

void update_pause_scene(void) {
    // Update audio mixer first (prevents crackling)
    if (audio_can_write()) {
        short *buf = audio_write_begin();
        rspq_wait();  // Flush RSPQ to exit highpri mode before mixer_poll (needed when rdpq_debug is active)
        save_apply_volume_settings_safe();  // Apply pending volume changes in safe window
        mixer_poll(buf, audio_get_buffer_length());
        audio_write_end();
    }

    joypad_poll();
    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);

    // Navigate menu with D-pad or stick
    static bool stickHeldUp = false;
    static bool stickHeldDown = false;
    bool stickUp = inputs.stick_y > 50;
    bool stickDown = inputs.stick_y < -50;

    if (pressed.d_up || (stickUp && !stickHeldUp)) {
        selectedOption--;
        if (selectedOption < 0) selectedOption = PAUSE_OPTION_COUNT - 1;
    }
    stickHeldUp = stickUp;

    if (pressed.d_down || (stickDown && !stickHeldDown)) {
        selectedOption++;
        if (selectedOption >= PAUSE_OPTION_COUNT) selectedOption = 0;
    }
    stickHeldDown = stickDown;

    // Confirm selection with A or Start
    if (pressed.a || pressed.start) {
        switch (selectedOption) {
            case PAUSE_RESUME:
                change_scene(GAME);
                break;
            case PAUSE_RESTART:
                game_restart_level();
                change_scene(GAME);
                break;
            case PAUSE_QUIT:
                save_force_save();  // Force save before quitting (bypass throttle)
                change_scene(TITLE);
                break;
        }
    }

    // Quick resume with B
    if (pressed.b) {
        change_scene(GAME);
    }

    // Cursor animation
    cursorBob += 0.15f;
}

void draw_pause_scene(void) {
    surface_t *disp = display_get();
    rdpq_attach(disp, NULL);

    // Dark semi-transparent overlay (simulated with solid dark gray)
    rdpq_clear(RGBA32(0x20, 0x20, 0x30, 0xFF));

    // Title
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 130, 60, "PAUSED");

    // Menu options
    const char* options[] = {
        "Resume",
        "Restart Level",
        "Quit to Title"
    };

    int startY = 110;
    int spacing = 25;

    for (int i = 0; i < PAUSE_OPTION_COUNT; i++) {
        int y = startY + i * spacing;

        if (i == selectedOption) {
            // Selected option - draw cursor
            int bobOffset = (int)(sinf(cursorBob) * 2.0f);
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 90 + bobOffset, y, ">");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 105, y, "%s", options[i]);
        } else {
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 105, y, "%s", options[i]);
        }
    }

    // Controls hint
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 70, 210, "A/Start:Select  B:Resume");

    rdpq_sync_full(NULL, NULL);  // Full sync before frame end
    rdpq_detach_show();
}
