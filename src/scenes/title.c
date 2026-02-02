#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/tpx.h>
#include "title.h"
#include "../scene.h"
#include "../save.h"
#include "../levels_generated.h"

#define PSYOPS_IMPLEMENTATION
#include "../PsyopsCode.h"

// Menu states
typedef enum {
    TITLE_MAIN,          // Main title screen - Press Start
    TITLE_SAVE_SELECT,   // New Game / Load Game selection
    TITLE_NEW_GAME,      // Choose slot for new game
    TITLE_LOAD_GAME,     // Choose which save to load
    TITLE_CONFIRM_DELETE // Confirm overwrite existing save
} TitleState;

static TitleState titleState = TITLE_MAIN;
static int menuSelection = 0;
static int deleteConfirmSlot = -1;

// Input debounce
static int inputDelay = 0;
#define INPUT_DELAY_FRAMES 8

// Stick input state
static bool stickHeldUp = false;
static bool stickHeldDown = false;
static bool stickHeldLeft = false;
static bool stickHeldRight = false;

void init_title_scene(void) {
    // Initialize save system
    debugf("Title: Initializing save system...\n");
    save_system_init();
    debugf("Title: Save system initialized, EEPROM=%d\n", g_saveSystem.eepromPresent);

    // Set level info for percentage calculation (from generated constants)
    save_set_level_info(TOTAL_BOLTS_IN_GAME, TOTAL_SCREWG_IN_GAME, REAL_LEVEL_COUNT);

    titleState = TITLE_MAIN;
    menuSelection = 0;
    inputDelay = 0;
    deleteConfirmSlot = -1;
}

void deinit_title_scene(void) {
    // Cleanup title screen resources here
}

// Check if any save files exist
static bool any_saves_exist(void) {
    for (int i = 0; i < SAVE_FILE_COUNT; i++) {
        if (save_slot_has_data(i)) return true;
    }
    return false;
}

void update_title_scene(void) {
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
    bool stickLeft = inputs.stick_x < -50;
    bool stickRight = inputs.stick_x > 50;

    // Check for cheat code input (1/60 second per frame)
    psyops_check_code(JOYPAD_PORT_1, 1.0f / 60.0f);

    // Input delay for menu navigation
    if (inputDelay > 0) {
        inputDelay--;
    }

    switch (titleState) {
        case TITLE_MAIN:
            if (pressed.start) {
                titleState = TITLE_SAVE_SELECT;
                menuSelection = 0;
                inputDelay = INPUT_DELAY_FRAMES;
            }
            // Debug shortcuts
            if (pressed.z) {
                change_scene(CUTSCENE_DEMO);
            }
            if (pressed.l) {
                change_scene(MAP_TEST);
            }
            if (pressed.r) {
                change_scene(DEBUG_MAP);
            }
            break;

        case TITLE_SAVE_SELECT:
            // Navigate: New Game / Load Game
            if (inputDelay == 0) {
                if (pressed.d_up || pressed.d_down ||
                    (stickUp && !stickHeldUp) || (stickDown && !stickHeldDown)) {
                    menuSelection = 1 - menuSelection;  // Toggle between 0 and 1
                    inputDelay = INPUT_DELAY_FRAMES;
                }
            }
            stickHeldUp = stickUp;
            stickHeldDown = stickDown;

            if (pressed.a) {
                if (menuSelection == 0) {
                    // New Game
                    titleState = TITLE_NEW_GAME;
                    menuSelection = 0;
                    inputDelay = INPUT_DELAY_FRAMES;
                } else {
                    // Load Game
                    if (any_saves_exist()) {
                        titleState = TITLE_LOAD_GAME;
                        menuSelection = 0;
                        inputDelay = INPUT_DELAY_FRAMES;
                    }
                    // If no saves, do nothing
                }
            }

            if (pressed.b) {
                titleState = TITLE_MAIN;
                inputDelay = INPUT_DELAY_FRAMES;
            }
            break;

        case TITLE_NEW_GAME:
            // Select a slot for new game
            if (inputDelay == 0) {
                if ((pressed.d_up || (stickUp && !stickHeldUp)) && menuSelection > 0) {
                    menuSelection--;
                    inputDelay = INPUT_DELAY_FRAMES;
                }
                if ((pressed.d_down || (stickDown && !stickHeldDown)) && menuSelection < SAVE_FILE_COUNT - 1) {
                    menuSelection++;
                    inputDelay = INPUT_DELAY_FRAMES;
                }
            }
            stickHeldUp = stickUp;
            stickHeldDown = stickDown;

            if (pressed.a) {
                if (save_slot_has_data(menuSelection)) {
                    // Slot has data - confirm overwrite
                    deleteConfirmSlot = menuSelection;
                    titleState = TITLE_CONFIRM_DELETE;
                    menuSelection = 1;  // Default to "No"
                    inputDelay = INPUT_DELAY_FRAMES;
                } else {
                    // Slot empty - create new save and start game
                    debugf("Title: Creating new save in slot %d\n", menuSelection);
                    save_create_new(menuSelection);
                    debugf("Title: Active save slot is now %d\n", g_saveSystem.activeSaveSlot);
                    if (psyops_is_unlocked()) {
                        change_scene(LEVEL_SELECT);
                    } else {
                        change_scene(GAME);
                    }
                }
            }

            if (pressed.b) {
                titleState = TITLE_SAVE_SELECT;
                menuSelection = 0;
                inputDelay = INPUT_DELAY_FRAMES;
            }
            break;

        case TITLE_LOAD_GAME:
            // Select which save to load
            if (inputDelay == 0) {
                if ((pressed.d_up || (stickUp && !stickHeldUp)) && menuSelection > 0) {
                    menuSelection--;
                    inputDelay = INPUT_DELAY_FRAMES;
                }
                if ((pressed.d_down || (stickDown && !stickHeldDown)) && menuSelection < SAVE_FILE_COUNT - 1) {
                    menuSelection++;
                    inputDelay = INPUT_DELAY_FRAMES;
                }
            }
            stickHeldUp = stickUp;
            stickHeldDown = stickDown;

            if (pressed.a) {
                if (save_slot_has_data(menuSelection)) {
                    save_load(menuSelection);
                    if (psyops_is_unlocked()) {
                        change_scene(LEVEL_SELECT);
                    } else {
                        change_scene(GAME);
                    }
                }
                // If slot empty, do nothing
            }

            if (pressed.b) {
                titleState = TITLE_SAVE_SELECT;
                menuSelection = 1;  // Return to Load Game option
                inputDelay = INPUT_DELAY_FRAMES;
            }
            break;

        case TITLE_CONFIRM_DELETE:
            // Yes/No confirmation for overwriting
            if (inputDelay == 0) {
                if (pressed.d_left || pressed.d_right ||
                    (stickLeft && !stickHeldLeft) || (stickRight && !stickHeldRight)) {
                    menuSelection = 1 - menuSelection;  // Toggle Yes/No
                    inputDelay = INPUT_DELAY_FRAMES;
                }
            }
            stickHeldLeft = stickLeft;
            stickHeldRight = stickRight;

            if (pressed.a) {
                if (menuSelection == 0) {
                    // Yes - delete and create new
                    save_delete(deleteConfirmSlot);
                    save_create_new(deleteConfirmSlot);
                    if (psyops_is_unlocked()) {
                        change_scene(LEVEL_SELECT);
                    } else {
                        change_scene(GAME);
                    }
                } else {
                    // No - go back
                    titleState = TITLE_NEW_GAME;
                    menuSelection = deleteConfirmSlot;
                    deleteConfirmSlot = -1;
                    inputDelay = INPUT_DELAY_FRAMES;
                }
            }

            if (pressed.b) {
                titleState = TITLE_NEW_GAME;
                menuSelection = deleteConfirmSlot;
                deleteConfirmSlot = -1;
                inputDelay = INPUT_DELAY_FRAMES;
            }
            break;
    }
}

// Helper to draw a save slot info
static void draw_save_slot(int slot, int x, int y, bool selected) {
    char buffer[64];

    // Selection indicator
    if (selected) {
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, x - 16, y, ">");
    }

    if (save_slot_has_data(slot)) {
        SaveFile* save = &g_saveSystem.saves[slot];
        int percent = save_calc_percentage(save);
        snprintf(buffer, sizeof(buffer), "File %d: %d%%", slot + 1, percent);
    } else {
        snprintf(buffer, sizeof(buffer), "File %d: Empty", slot + 1);
    }

    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, x, y, "%s", buffer);
}

void draw_title_scene(void) {
    surface_t *disp = display_get();
    rdpq_attach(disp, NULL);
    rdpq_clear(RGBA32(0x20, 0x20, 0x40, 0xFF));

    switch (titleState) {
        case TITLE_MAIN:
            // Draw title text
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 100, 80, "MY N64 GAME");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 80, 130, "Press START to play");

            // Show cheat code status
            if (psyops_is_unlocked()) {
                rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 80, 200, "DEBUG MODE UNLOCKED!");
            }

            // Debug controls hint (smaller)
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 60, 220, "Z:Cutscene L:MapTest R:Debug");
            break;

        case TITLE_SAVE_SELECT:
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 100, 60, "SELECT OPTION");

            // New Game option
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 100, 100,
                "%s New Game", menuSelection == 0 ? ">" : " ");

            // Load Game option (grayed out if no saves)
            if (any_saves_exist()) {
                rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 100, 120,
                    "%s Load Game", menuSelection == 1 ? ">" : " ");
            } else {
                rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 100, 120,
                    "  Load Game (No saves)");
            }

            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 80, 200, "A:Select  B:Back");
            break;

        case TITLE_NEW_GAME:
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 80, 40, "SELECT SAVE SLOT");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 60, 60, "(Existing saves will be erased)");

            // Draw save slots
            for (int i = 0; i < SAVE_FILE_COUNT; i++) {
                draw_save_slot(i, 60, 100 + i * 30, menuSelection == i);
            }

            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 80, 210, "A:Select  B:Back");
            break;

        case TITLE_LOAD_GAME:
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 100, 40, "LOAD GAME");

            // Draw save slots
            for (int i = 0; i < SAVE_FILE_COUNT; i++) {
                draw_save_slot(i, 60, 100 + i * 30, menuSelection == i);
            }

            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 80, 210, "A:Load  B:Back");
            break;

        case TITLE_CONFIRM_DELETE:
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 60, 80, "Overwrite existing save?");

            // Show what will be lost
            if (deleteConfirmSlot >= 0 && save_slot_has_data(deleteConfirmSlot)) {
                SaveFile* save = &g_saveSystem.saves[deleteConfirmSlot];
                rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 60, 110,
                    "File %d: %d%% complete", deleteConfirmSlot + 1, save_calc_percentage(save));
            }

            // Yes/No buttons
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 80, 170,
                "%sYes     %sNo",
                menuSelection == 0 ? "[" : " ",
                menuSelection == 1 ? "[" : " ");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 108, 170, "]");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 172, 170, "]");

            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 80, 210, "A:Confirm  B:Cancel");
            break;
    }

    rdpq_sync_full(NULL, NULL);  // Full sync before frame end
    rdpq_detach_show();
}
