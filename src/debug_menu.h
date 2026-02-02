// ============================================================
// DEBUG MENU SYSTEM
// ============================================================
// Shared debug menu for all scenes. Uses the UI system for rendering.
// Include this file in any scene that needs debug functionality.
//
// Usage:
//   1. Call debug_menu_init() once at scene init
//   2. Call debug_menu_add_*() to add menu items
//   3. Call debug_menu_update() each frame (returns true if input consumed)
//   4. Call debug_menu_draw() to render
//   5. Call debug_menu_reset() when changing scenes

#ifndef DEBUG_MENU_H
#define DEBUG_MENU_H

#include <libdragon.h>
#include <stdbool.h>
#include "ui.h"
#include "PsyopsCode.h"

// ============================================================
// CONFIGURATION
// ============================================================

#define DEBUG_MENU_MAX_ITEMS 16
#define DEBUG_MENU_MAX_NAME_LEN 20

// Button font ID (registered in main.c as font 2)
#define DEBUG_BUTTON_FONT 2

// ============================================================
// MENU ITEM TYPES
// ============================================================

typedef enum {
    DEBUG_ITEM_FLOAT,      // Adjustable float value
    DEBUG_ITEM_INT,        // Adjustable integer value
    DEBUG_ITEM_BOOL,       // Toggle on/off
    DEBUG_ITEM_ACTION,     // Execute callback on select
} DebugItemType;

typedef void (*DebugActionCallback)(void);

typedef struct {
    char name[DEBUG_MENU_MAX_NAME_LEN];
    DebugItemType type;
    union {
        struct {
            float* value;
            float min;
            float max;
            float step;
        } floatVal;
        struct {
            int* value;
            int min;
            int max;
            int step;
        } intVal;
        struct {
            bool* value;
        } boolVal;
        struct {
            DebugActionCallback callback;
        } action;
    };
    bool active;
} DebugMenuItem;

// ============================================================
// MENU STATE
// ============================================================

typedef struct {
    DebugMenuItem items[DEBUG_MENU_MAX_ITEMS];
    int itemCount;
    int selection;
    bool isOpen;
    char title[32];
} DebugMenuState;

// Global debug menu state
static DebugMenuState g_debugMenu = {0};

// ============================================================
// API FUNCTIONS
// ============================================================

// Initialize the debug menu system
static inline void debug_menu_init(const char* title) {
    memset(&g_debugMenu, 0, sizeof(g_debugMenu));
    strncpy(g_debugMenu.title, title, sizeof(g_debugMenu.title) - 1);
}

// Reset menu (call when changing scenes)
static inline void debug_menu_reset(void) {
    g_debugMenu.itemCount = 0;
    g_debugMenu.selection = 0;
    g_debugMenu.isOpen = false;
}

// Add a float value item
static inline void debug_menu_add_float(const char* name, float* value, float min, float max, float step) {
    if (g_debugMenu.itemCount >= DEBUG_MENU_MAX_ITEMS) return;

    DebugMenuItem* item = &g_debugMenu.items[g_debugMenu.itemCount++];
    strncpy(item->name, name, DEBUG_MENU_MAX_NAME_LEN - 1);
    item->type = DEBUG_ITEM_FLOAT;
    item->floatVal.value = value;
    item->floatVal.min = min;
    item->floatVal.max = max;
    item->floatVal.step = step;
    item->active = true;
}

// Add an integer value item
static inline void debug_menu_add_int(const char* name, int* value, int min, int max, int step) {
    if (g_debugMenu.itemCount >= DEBUG_MENU_MAX_ITEMS) return;

    DebugMenuItem* item = &g_debugMenu.items[g_debugMenu.itemCount++];
    strncpy(item->name, name, DEBUG_MENU_MAX_NAME_LEN - 1);
    item->type = DEBUG_ITEM_INT;
    item->intVal.value = value;
    item->intVal.min = min;
    item->intVal.max = max;
    item->intVal.step = step;
    item->active = true;
}

// Add a boolean toggle item
static inline void debug_menu_add_bool(const char* name, bool* value) {
    if (g_debugMenu.itemCount >= DEBUG_MENU_MAX_ITEMS) return;

    DebugMenuItem* item = &g_debugMenu.items[g_debugMenu.itemCount++];
    strncpy(item->name, name, DEBUG_MENU_MAX_NAME_LEN - 1);
    item->type = DEBUG_ITEM_BOOL;
    item->boolVal.value = value;
    item->active = true;
}

// Add an action item (executes callback when selected)
static inline void debug_menu_add_action(const char* name, DebugActionCallback callback) {
    if (g_debugMenu.itemCount >= DEBUG_MENU_MAX_ITEMS) return;

    DebugMenuItem* item = &g_debugMenu.items[g_debugMenu.itemCount++];
    strncpy(item->name, name, DEBUG_MENU_MAX_NAME_LEN - 1);
    item->type = DEBUG_ITEM_ACTION;
    item->action.callback = callback;
    item->active = true;
}

// Check if menu is open
static inline bool debug_menu_is_open(void) {
    return g_debugMenu.isOpen;
}

// Open/close menu
static inline void debug_menu_open(void) {
    g_debugMenu.isOpen = true;
}

static inline void debug_menu_close(void) {
    g_debugMenu.isOpen = false;
}

static inline void debug_menu_toggle(void) {
    g_debugMenu.isOpen = !g_debugMenu.isOpen;
}

// ============================================================
// UPDATE FUNCTION
// ============================================================
// Returns true if debug system consumed input

static inline bool debug_menu_update(joypad_port_t port, float deltaTime) {
    // Check cheat code
    psyops_check_code(port, deltaTime);

    // If not unlocked, nothing to do
    if (!psyops_is_unlocked()) {
        return false;
    }

    joypad_buttons_t pressed = joypad_get_buttons_pressed(port);
    joypad_buttons_t held = joypad_get_buttons_held(port);
    joypad_inputs_t inputs = joypad_get_inputs(port);

    // Track stick positions
    bool stickUp = inputs.stick_y > 50;
    bool stickDown = inputs.stick_y < -50;
    bool stickLeft = inputs.stick_x < -50;
    bool stickRight = inputs.stick_x > 50;
    static bool stickHeldUp = false;
    static bool stickHeldDown = false;

    // D-Down toggles menu (when cheat unlocked) - D-pad only, not stick
    static int toggleCooldown = 0;
    if (toggleCooldown > 0) toggleCooldown--;

    if (pressed.d_down && toggleCooldown == 0 && !g_debugMenu.isOpen) {
        debug_menu_toggle();
        toggleCooldown = 10;
        return true;
    }

    if (!g_debugMenu.isOpen) {
        stickHeldUp = stickUp;
        stickHeldDown = stickDown;
        return false;
    }

    // Menu navigation
    if (pressed.d_up || (stickUp && !stickHeldUp)) {
        g_debugMenu.selection--;
        if (g_debugMenu.selection < 0) {
            g_debugMenu.selection = g_debugMenu.itemCount - 1;
        }
    }
    stickHeldUp = stickUp;

    if ((pressed.d_down || (stickDown && !stickHeldDown)) && toggleCooldown == 0) {
        g_debugMenu.selection++;
        if (g_debugMenu.selection >= g_debugMenu.itemCount) {
            g_debugMenu.selection = 0;
        }
    }
    stickHeldDown = stickDown;

    // Get current item
    if (g_debugMenu.itemCount == 0) {
        if (pressed.b) {
            debug_menu_close();
        }
        return true;
    }

    DebugMenuItem* item = &g_debugMenu.items[g_debugMenu.selection];

    // Handle input based on item type
    switch (item->type) {
        case DEBUG_ITEM_FLOAT: {
            float delta = 0.0f;
            if (held.d_left || stickLeft) delta = -item->floatVal.step;
            if (held.d_right || stickRight) delta = item->floatVal.step;
            if (delta != 0.0f) {
                *item->floatVal.value += delta;
                if (*item->floatVal.value < item->floatVal.min) {
                    *item->floatVal.value = item->floatVal.min;
                }
                if (*item->floatVal.value > item->floatVal.max) {
                    *item->floatVal.value = item->floatVal.max;
                }
            }
            break;
        }
        case DEBUG_ITEM_INT: {
            int delta = 0;
            if (pressed.d_left || stickLeft) delta = -item->intVal.step;
            if (pressed.d_right || stickRight) delta = item->intVal.step;
            if (delta != 0) {
                *item->intVal.value += delta;
                if (*item->intVal.value < item->intVal.min) {
                    *item->intVal.value = item->intVal.min;
                }
                if (*item->intVal.value > item->intVal.max) {
                    *item->intVal.value = item->intVal.max;
                }
            }
            break;
        }
        case DEBUG_ITEM_BOOL: {
            if (pressed.a || pressed.d_left || pressed.d_right) {
                *item->boolVal.value = !*item->boolVal.value;
            }
            break;
        }
        case DEBUG_ITEM_ACTION: {
            if (pressed.a) {
                if (item->action.callback) {
                    item->action.callback();
                }
            }
            break;
        }
    }

    // B closes menu
    if (pressed.b) {
        debug_menu_close();
    }

    return true;  // Menu consumed input
}

// ============================================================
// DRAW FUNCTION
// ============================================================

static inline void debug_menu_draw(void) {
    // Always show "DBG" indicator if unlocked
    if (!psyops_is_unlocked()) {
        return;
    }

    rdpq_set_prim_color(RGBA32(0x00, 0xFF, 0x00, 0xFF));
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 5, 12, "DBG");

    if (!g_debugMenu.isOpen) {
        return;
    }

    // Calculate menu dimensions
    int itemCount = g_debugMenu.itemCount;
    if (itemCount == 0) itemCount = 1;  // At least show "No items"

    int menuWidth = 200;
    int menuHeight = 50 + itemCount * 16;
    int menuX = 60;
    int menuY = 30;

    // Draw styled box background
    ui_draw_box(menuX, menuY, menuWidth, menuHeight, UI_COLOR_BG, RGBA32(0x00, 0xFF, 0x00, 0xFF));

    // Draw title
    rdpq_set_prim_color(RGBA32(0x00, 0xFF, 0x00, 0xFF));
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, menuX + 15, menuY + 15, "%s", g_debugMenu.title);

    // Draw control hints with button icons (extra spacing after icons)
    int hintY = menuY + 28;
    rdpq_set_prim_color(RGBA32(0x80, 0xFF, 0x80, 0xFF));

    // D-pad hint
    rdpq_text_printf(NULL, DEBUG_BUTTON_FONT, menuX + 15, hintY, "tg");  // D-pad up/down
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, menuX + 42, hintY, " Nav");

    // Left/Right for adjust
    rdpq_text_printf(NULL, DEBUG_BUTTON_FONT, menuX + 78, hintY, "fh");  // D-pad left/right
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, menuX + 105, hintY, " Adj");

    // B to close
    rdpq_text_printf(NULL, DEBUG_BUTTON_FONT, menuX + 142, hintY, "b");
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, menuX + 157, hintY, " Close");

    // Draw menu items
    int itemStartY = menuY + 45;

    if (g_debugMenu.itemCount == 0) {
        rdpq_set_prim_color(RGBA32(0x80, 0x80, 0x80, 0xFF));
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, menuX + 15, itemStartY, "No items");
        return;
    }

    for (int i = 0; i < g_debugMenu.itemCount; i++) {
        int y = itemStartY + i * 16;
        DebugMenuItem* item = &g_debugMenu.items[i];

        bool isSelected = (i == g_debugMenu.selection);

        // Selection indicator
        if (isSelected) {
            rdpq_set_prim_color(RGBA32(0x00, 0xFF, 0x00, 0xFF));
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, menuX + 15, y, ">");
        }

        // Item name
        rdpq_set_prim_color(isSelected ? RGBA32(0x00, 0xFF, 0x00, 0xFF) : RGBA32(0x80, 0x80, 0x80, 0xFF));
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, menuX + 27, y, "%-12s", item->name);

        // Item value
        int valueX = menuX + 130;

        switch (item->type) {
            case DEBUG_ITEM_FLOAT:
                rdpq_set_prim_color(isSelected ? RGBA32(0xFF, 0xFF, 0x00, 0xFF) : RGBA32(0x80, 0x80, 0x80, 0xFF));
                rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, valueX, y, "%6.2f", *item->floatVal.value);
                break;

            case DEBUG_ITEM_INT:
                rdpq_set_prim_color(isSelected ? RGBA32(0xFF, 0xFF, 0x00, 0xFF) : RGBA32(0x80, 0x80, 0x80, 0xFF));
                rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, valueX, y, "%6d", *item->intVal.value);
                break;

            case DEBUG_ITEM_BOOL:
                if (*item->boolVal.value) {
                    rdpq_set_prim_color(RGBA32(0x00, 0xFF, 0x00, 0xFF));
                    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, valueX, y, "   ON");
                } else {
                    rdpq_set_prim_color(RGBA32(0xFF, 0x60, 0x60, 0xFF));
                    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, valueX, y, "  OFF");
                }
                break;

            case DEBUG_ITEM_ACTION:
                if (isSelected) {
                    rdpq_set_prim_color(RGBA32(0xFF, 0xFF, 0x00, 0xFF));
                    rdpq_text_printf(NULL, DEBUG_BUTTON_FONT, valueX + 20, y, "a");
                }
                break;
        }
    }
}

#endif // DEBUG_MENU_H
