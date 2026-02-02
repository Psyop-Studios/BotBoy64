#include <libdragon.h>
#include "level_select.h"
#include "../scene.h"
#include "../levels.h"
#include "../save.h"

// Selected level (shared with game scene)
int selectedLevelID = 0;

// If true, game scene starts with iris opening effect
bool startWithIrisOpen = false;

// If true, menu scene starts with iris opening effect (returning from game)
bool menuStartWithIrisOpen = false;

// Current selection
static int currentSelection = 0;

// Stick input state
static bool stickHeldUp = false;
static bool stickHeldDown = false;

void init_level_select_scene(void) {
    currentSelection = 0;
    stickHeldUp = false;
    stickHeldDown = false;
}

void update_level_select_scene(void) {
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

    // Track stick positions
    bool stickUp = inputs.stick_y > 50;
    bool stickDown = inputs.stick_y < -50;

    // Navigate with D-Pad or stick
    if (pressed.d_up || (stickUp && !stickHeldUp)) {
        if (currentSelection > 0) {
            currentSelection--;
        }
    }
    stickHeldUp = stickUp;

    if (pressed.d_down || (stickDown && !stickHeldDown)) {
        if (currentSelection < LEVEL_COUNT - 1) {
            currentSelection++;
        }
    }
    stickHeldDown = stickDown;

    // Select level with A button
    if (pressed.a) {
        selectedLevelID = currentSelection;
        change_scene(GAME);
    }

    // Back to title with B button
    if (pressed.b) {
        change_scene(TITLE);
    }
}

void draw_level_select_scene(void) {
    surface_t *disp = display_get();
    rdpq_attach(disp, NULL);
    rdpq_clear(RGBA32(0x20, 0x20, 0x40, 0xFF));

    // Title
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 80, 10, "%s", "=== LEVEL SELECT ===");
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 60, 25, "%s", "D-Pad: Navigate  A: Select");
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 60, 35, "%s", "B: Back to Title");

    // Draw all 12 levels
    int startY = 55;
    int lineHeight = 12;

    for (int i = 0; i < LEVEL_COUNT; i++) {
        int y = startY + i * lineHeight;

        const char* levelName = ALL_LEVELS[i]->name;

        // Adjust spacing for double-digit level numbers
        int colonX = (i >= 9) ? 122 : 114;
        int nameX = (i >= 9) ? 130 : 122;

        if (i == currentSelection) {
            // Highlight selected level
            rdpq_set_mode_standard();
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            rdpq_set_prim_color(RGBA32(100, 100, 200, 255));
            rdpq_fill_rectangle(40, y - 9, 280, y + 2);

            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 50, y, "%s", "> Level ");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 106, y, "%d", i + 1);
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, colonX, y, "%s", ":");
            // Print level name with %s to avoid treating it as a format string
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, nameX, y, "%s", levelName);
        } else {
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 50, y, "%s", "  Level ");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 106, y, "%d", i + 1);
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, colonX, y, "%s", ":");
            // Print level name with %s to avoid treating it as a format string
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, nameX, y, "%s", levelName);
        }
    }

    rdpq_sync_full(NULL, NULL);  // Full sync before frame end
    rdpq_detach_show();
}

void cleanup_level_select_scene(void) {
    // Nothing to clean up
}
