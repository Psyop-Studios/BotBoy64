#ifndef MENU_UI_H
#define MENU_UI_H

#include <libdragon.h>
#include <stdbool.h>

#define MAX_MENU_ITEMS 16

typedef void (*MenuCallback)(void);

typedef struct {
    const char* label;
    MenuCallback callback;
} MenuItem;

typedef struct {
    const char* title;
    MenuItem items[MAX_MENU_ITEMS];
    int itemCount;
    int selectedIndex;
    bool isActive;
    int x, y;           // Position
    int itemSpacing;    // Pixels between items
    bool stickHeldUp;   // Stick input state
    bool stickHeldDown;
} MenuUI;

// Initialize a menu
static inline void menu_init(MenuUI* menu, const char* title, int x, int y) {
    menu->title = title;
    menu->itemCount = 0;
    menu->selectedIndex = 0;
    menu->isActive = false;
    menu->x = x;
    menu->y = y;
    menu->itemSpacing = 20;
}

// Add an item to the menu
static inline void menu_add_item(MenuUI* menu, const char* label, MenuCallback callback) {
    if (menu->itemCount >= MAX_MENU_ITEMS) return;
    menu->items[menu->itemCount].label = label;
    menu->items[menu->itemCount].callback = callback;
    menu->itemCount++;
}

// Show/hide menu
static inline void menu_open(MenuUI* menu) {
    menu->isActive = true;
    menu->selectedIndex = 0;
}

static inline void menu_close(MenuUI* menu) {
    menu->isActive = false;
}

static inline bool menu_is_open(MenuUI* menu) {
    return menu->isActive;
}

// Handle input - returns true if menu consumed the input
static inline bool menu_update(MenuUI* menu, joypad_port_t port) {
    if (!menu->isActive) return false;

    joypad_buttons_t pressed = joypad_get_buttons_pressed(port);
    joypad_inputs_t inputs = joypad_get_inputs(port);

    // Track stick positions
    bool stickUp = inputs.stick_y > 50;
    bool stickDown = inputs.stick_y < -50;

    // Navigate up
    if (pressed.d_up || (stickUp && !menu->stickHeldUp)) {
        menu->selectedIndex--;
        if (menu->selectedIndex < 0) {
            menu->selectedIndex = menu->itemCount - 1;
        }
    }
    menu->stickHeldUp = stickUp;

    // Navigate down
    if (pressed.d_down || (stickDown && !menu->stickHeldDown)) {
        menu->selectedIndex++;
        if (menu->selectedIndex >= menu->itemCount) {
            menu->selectedIndex = 0;
        }
    }
    menu->stickHeldDown = stickDown;

    // Select item
    if (pressed.a) {
        if (menu->items[menu->selectedIndex].callback) {
            menu->items[menu->selectedIndex].callback();
        }
        return true;
    }

    // Close menu
    if (pressed.b) {
        menu_close(menu);
        return true;
    }

    return false;
}

// Draw the menu
static inline void menu_draw(MenuUI* menu) {
    if (!menu->isActive) return;

    int x = menu->x;
    int y = menu->y;

    // Draw title
    if (menu->title) {
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, x, y, "%s", menu->title);
        y += 30;
    }

    // Draw items
    for (int i = 0; i < menu->itemCount; i++) {
        const char* prefix = (i == menu->selectedIndex) ? "> " : "  ";
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, x, y + i * menu->itemSpacing,
            "%s%s", prefix, menu->items[i].label);
    }
}

#endif // MENU_UI_H
